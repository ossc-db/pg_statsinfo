# pg_statsinfo Partial Write Reproducer

This script simulates a **partially written PostgreSQL CSV log entry** to reproduce a known issue in `pg_statsinfo`, where reading an incomplete log line causes parsing errors and incorrect offset tracking.

It helps validate the robustness of log parsing logic in `pg_statsinfo`, especially in scenarios where the PostgreSQL backend log entry is flushed in two parts.

---

## Prerequisites

- PostgreSQL with `pg_statsinfo` installed and configured
- Python 3.6 or higher
- Python packages:
  - `psycopg2`
  - `pytz` (optional, if timestamps are involved)

> ⚠️ If your PostgreSQL installation does **not** embed `RPATH`, make sure to set `LD_LIBRARY_PATH` to include the `lib` directory of PostgreSQL:

```bash
export LD_LIBRARY_PATH=/path/to/postgres/lib:$LD_LIBRARY_PATH



## Configuration:

Make sure PostgreSQL is running with the following parameters enabled in postgresql.conf:

logging_collector = on
log_destination = 'csvlog'
log_directory = 'log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.csv'
log_statement = 'all'
log_min_messages = LOG

# pg_statsinfo-specific settings
pg_statsinfo.repolog_buffer = 1      # Set a small buffer size so pg_statsinfo inserts 
                                     # parsed logs immediately, making errors easier to 
                                     # reproduce promptly
pg_statsinfo.repolog_interval = 60

------
After updating postgresql.conf, restart the PostgreSQL instance for the changes to take effect.




## Usage:

python3 tools/reproduce_partial_write.py \
    --pg-bin /path/to/postgres/bin \
    --log-dir /path/to/pg_log \
    --pg-port 5432

