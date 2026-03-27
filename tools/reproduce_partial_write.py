import argparse
import os
import time
import glob
import re
import pytz
import psycopg2
import subprocess
import sys
from datetime import datetime

MAX_RETRIES = 5
RETRY_DELAY = 5  # seconds


def get_current_timestamp_jst():
    jst = pytz.timezone("Asia/Tokyo")
    return datetime.now(jst).strftime("%Y-%m-%d %H:%M:%S JST")


def parse_to_hourly_timestamp(timestamp_str):
    timestamp = datetime.strptime(timestamp_str.replace(" JST", ""), "%Y-%m-%d %H:%M:%S")
    rounded_time = timestamp.replace(minute=0, second=0, microsecond=0)
    return rounded_time.strftime("%Y-%m-%d %H:%M:%S")


def get_latest_logfile(log_dir):
    log_files = sorted(glob.glob(os.path.join(log_dir, "postgresql-*.csv")), reverse=True)
    return log_files[0] if log_files else None


def generate_new_logfile_name(latest_logfile):
    match = re.search(r"(postgresql-\d{4}-\d{2}-\d{2}_)(\d{6})", latest_logfile)
    if match:
        prefix, number = match.groups()
        new_number = f"{int(number) + 1:06d}"
        return latest_logfile.replace(f"_{number}.csv", f"_{new_number}.csv")
    return None


def create_partial_csvlog(log_dir):
    latest_logfile = get_latest_logfile(log_dir)
    if not latest_logfile:
        raise RuntimeError("No CSV log files found.")

    new_logfile = generate_new_logfile_name(latest_logfile)
    if not new_logfile:
        raise RuntimeError("Failed to generate new log file name.")

    full_timestamp = get_current_timestamp_jst()
    partial_timestamp = full_timestamp[:14]  # Partial write: YYYY-MM-DD HH:

    with open(new_logfile, "w") as f:
        f.write(partial_timestamp)

    print(f"Partial CSV log created: {new_logfile}")
    return new_logfile, full_timestamp


def append_to_logfile(logfile, content):
    time.sleep(5)
    with open(logfile, "a") as f:
        f.write(content + "\n")
    print(f"✅ Appended to {logfile}:{content}")


def check_pg_statsinfo_log(log_dir, min_count=2):
    log_path = os.path.join(log_dir, "pg_statsinfo.log")
    if not os.path.exists(log_path):
        return False

    with open(log_path, "r") as f:
        lines = f.readlines()

    count = sum(1 for line in lines[-50:] if "WARNING:  pg_statsinfo: cannot parse csvlog column" in line)
    return count >= min_count


def execute_query(query, db_config):
    conn = psycopg2.connect(**db_config)
    cur = conn.cursor()
    cur.execute(query)
    result = cur.fetchall()
    conn.commit()
    cur.close()
    conn.close()
    return result


def check_pg_statsinfo_error(log_dir):
    error_strings = [
        "ERROR:  invalid input syntax for type timestamp with time zone",
        "ERROR:  pg_statsinfo: query failed: ERROR:  date/time "
    ]
    count = 0
    for file in os.listdir(log_dir):
        path = os.path.join(log_dir, file)
        if os.path.isfile(path):
            with open(path, "r") as f:
                count += sum(1 for line in f if any(err in line for err in error_strings))
    return count > 0


def check_postgres_running(pg_bin, pg_port):
    pg_isready_path = os.path.join(pg_bin, "pg_isready")
    try:
        result = subprocess.run([pg_isready_path, "-p", str(pg_port)], check=True)
        if result.returncode != 0:
            print("❌ PostgreSQL server does not appear to be running:")
            print(result.stderr.strip())
            exit(1)
        else:
            print("✅ PostgreSQL server is running.")
    except Exception as e:
        print(f"❌ Unexpected error while checking PostgreSQL status: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print("⚠️ 'pg_isready' not found. Please ensure PostgreSQL tools are installed and in PATH.")
        exit(1)


def main():
    parser = argparse.ArgumentParser(description="Reproduce partial write parsing issue in pg_statsinfo")
    parser.add_argument("--data-dir", required=True, help="Path to PostgreSQL data directory")
    parser.add_argument("--pg-bin", required=True, help="Path to PostgreSQL binaries")
    parser.add_argument("--csv-log", required=True, help="Path to CSV log directory")
    parser.add_argument("--pg-port", default="5432", help="PostgreSQL port")
    args = parser.parse_args()

    db_config = {
        "dbname": "postgres",
        "user": "postgres",
        "host": "localhost",
        "port": args.pg_port
    }

    print("🔍 Checking PostgreSQL status...")
    check_postgres_running(args.pg_bin, args.pg_port)

    print("Creating partial log entry...")
    partial_logfile, ts_str = create_partial_csvlog(args.csv_log)

    print("Waiting for pg_statsinfo to detect parsing warning...")
    for _ in range(12):
        if check_pg_statsinfo_log(args.csv_log):
            break
        print("Waiting for WARNING in pg_statsinfo.log...")
        time.sleep(5)
    else:
        print("✖️ Parsing warning not detected. Test scenario failed.")
        return

    print("Appending remaining part of the line...")
    remaining = ts_str[14:]
    content = f"{remaining},,,43168,,abcd.1234,3025,,{ts_str},,0,LOG,00000,\"checkpoint starting: time postgres invalid2\",,,,,,,,,\"\",\"checkpointer\",,0"
    append_to_logfile(partial_logfile, content)

    query_ts = parse_to_hourly_timestamp(ts_str)
    query = f"SELECT count(*) FROM statsrepo.log WHERE timestamp >= '{query_ts}' AND message LIKE '%postgres invalid2%';"

    for attempt in range(1, MAX_RETRIES + 1):
        count = execute_query(query, db_config)[0][0]
        if count > 0:
            print(f"✅ Validation Passed: {count} matching entries found.")
            break
        else:
            print(f"Attempt {attempt}/{MAX_RETRIES} failed. Retrying in {RETRY_DELAY} seconds...")
            time.sleep(RETRY_DELAY)
    else:
        print("✖️ Validation Failed after multiple retries.")

    if check_pg_statsinfo_error(args.csv_log):
        print("\u274C [FAILED] Timestamp parse error detected.")
    else:
        print("\u2705 [PASSED] No timestamp errors found.")


if __name__ == "__main__":
    main()

