CREATE OR REPLACE FUNCTION statsrepo.input_data(
	IN instid			bigint,
	IN systemid			text,
	IN host				text,
	IN port				integer,
	IN pg_version		text,
	IN snapid_offset	bigint
) RETURNS void AS
$$
	--
	-- PostgreSQL database dump
	--
	SET client_min_messages = warning;

	--
	-- Data for Name: instance; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.instance VALUES ($1, $2, $3, $4, $5, X'FF000000'::bigint, 8192, 24, 24, 4);

	--
	-- Data for Name: snapshot; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.snapshot VALUES ($6, $1, '2012-11-01 00:00:00+09', '', '00:00:01', 270336, '871');
	INSERT INTO statsrepo.snapshot VALUES ($6 + 1, $1, '2012-11-01 00:01:00+09', '', '00:00:01', 8192, '22507');
	INSERT INTO statsrepo.snapshot VALUES ($6 + 2, $1, '2012-11-01 00:02:00+09', '', '00:00:01', 0, '22522');
	INSERT INTO statsrepo.snapshot VALUES ($6 + 3, $1, '2012-11-01 00:03:00+09', '', '00:00:01', 16384, '22526');

	--
	-- Data for Name: activity; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.activity VALUES ($6,     240, 120,  60,  240,  55);
	INSERT INTO statsrepo.activity VALUES ($6 + 1, 480, 240, 120,  480, 110);
	INSERT INTO statsrepo.activity VALUES ($6 + 2, 240, 120,  60,  240,  55);
	INSERT INTO statsrepo.activity VALUES ($6 + 3, 480, 240, 120,  480, 110);

	--
	-- Data for Name: alert_message; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.alert_message VALUES ($6 + 1, 'too many transactions in snapshots between ''2012-11-01 00:00:00'' and ''2012-11-01 00:01:00'' --- 1585 Transactions/sec (threshold = 1000 Transactions/sec)');
	INSERT INTO statsrepo.alert_message VALUES ($6 + 2, 'too many transactions in snapshots between ''2012-11-01 00:01:00'' and ''2012-11-01 00:02:00'' --- 1688 Transactions/sec (threshold = 1000 Transactions/sec)');
	INSERT INTO statsrepo.alert_message VALUES ($6 + 2, 'dead tuple size exceeds threshold in snapshot ''2012-11-01 00:02:00'' --- 245.15 MiB (threshold = 100 MiB)');

	--
	-- Data for Name: archive; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.archive VALUES ($6, 0, NULL, NULL, 0, NULL, NULL, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.archive VALUES ($6 + 1, 1, '000000010000000000000002', '2012-11-01 00:00:30+09', 0, NULL, NULL, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.archive VALUES ($6 + 2, 3, '000000010000000000000004', '2012-11-01 00:01:30+09', 1, '000000010000000000000003', '2012-11-01 00:01:00+09', '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.archive VALUES ($6 + 3, 4, '000000010000000000000005', '2012-11-01 00:02:30+09', 1, '000000010000000000000003', '2012-11-01 00:01:00+09', '2012-11-01 00:00:00+09');

	--
	-- Data for Name: autoanalyze; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_branches', 0,0,0,0,0, 0.841124545,  0.1,0.0);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_tellers',  0,0,0,0,0, 0.0154855544, 0.2,0.1);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_history',  0,0,0,0,0, 0.0700000003, 0.3,0.0);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_accounts', 0,0,0,0,0, 0.389999986,  0.4,0.2);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_branches', 0,0,0,0,0, 0.845144487,  0.5,0.0);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_tellers',  0,0,0,0,0, 0.0154854855, 0.6,0.3);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_history',  0,0,0,0,0, 0.159999996,  0.7,0.0);
	INSERT INTO statsrepo.autoanalyze VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_accounts', 0,0,0,0,0, 0.280000001,  0.8,0.4);

	--
	-- Data for Name: autoanalyze_cancel; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.autoanalyze_cancel VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_branches', 'VACUUM-01');
	INSERT INTO statsrepo.autoanalyze_cancel VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_accounts', 'VACUUM-01');
	INSERT INTO statsrepo.autoanalyze_cancel VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_branches', 'VACUUM-02');

	--
	-- Data for Name: autovacuum; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:00:30+09', 'template1', 'pg_catalog', 'pg_statistic', 1, 0, 28, 28, 100, 129, 404, 0, '811', 64.91, 61.581, 63, 39, 37, 92, 34, 185836, 0, NULL, NULL, '811', NULL, 15, 60, 164, 2, 16, 57.14, 178, '{pg_statistic_relid_att_inh_index}', '{5}', '{0}', '{0}', '{0}', 0.214, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'pg_catalog', 'pg_statistic', 1, 0, 28, 28, 100, 62, 415, 0, '824', 18.81, 37.62, 99, 3, 6, 58, 15, 70274, 0, NULL, NULL, NULL, NULL, 15, 60, 164, 2, 16, 57.14, 178, '{pg_statistic_relid_att_inh_index}', '{5}', '{0}', '{0}', '{0}', 0.086, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'pg_catalog', 'pg_attribute', 1, 0, 88, 51, 57.95, 2, 4678, 0, '825', 122.07, 0, 145, 14, 0, 44, 1, 11696, 0, NULL, NULL, '800', NULL, 15, 60, 164, 2, 3, 3.41, 88, '{pg_attribute_relid_attnam_index,pg_attribute_relid_attnum_index}', '{22,15}', '{0,0}', '{0,0}', '{0,0}', 0.118, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_branches', 0, 0, 1, 1, 100, 113, 10, 0, '11768', 0, 0, 22, 0, 0, 3, 0, 608, 0, NULL, NULL, '11744', NULL, 15, 60, 164, 4, 0, 0, 0, '{}', '{}', '{}', '{}', '{}', 0, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'statsrepo', 'column_20221219', 0, 0, 22, 22, 100, 0, 1225, 0, '11768', 54.825, 82.237, 76, 2, 3, 23, 1, 9683, 0, NULL, NULL, '810', NULL, 15, 60, 164, 4, 0, 0, 0, '{}', '{}', '{}', '{}', '{}', 0.012, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:02:30+09', 'postgres', 'public', 'pgbench_history', 0, 0, 70, 70, 100, 0, 10920, 0, '11768', 8.351, 16.702, 145, 2, 4, 71, 1, 12515, 0, NULL, NULL, '845', NULL, 15, 60, 164, 4, 0, 0, 0, '{}', '{}', '{}', '{}', '{}', 0.012, 0);
	INSERT INTO statsrepo.autovacuum VALUES ($1, '2012-11-01 00:02:30+09', 'postgres', 'pg_catalog', 'pg_statistic', 1, 0, 33, 33, 100, 21, 583, 0, '27293', 0, 0, 112, 0, 0, 57, 0, 4041, 0, NULL, NULL, NULL, NULL, 15, 60, 164, 2, 16, 48.48, 138, '{pg_statistic_relid_att_inh_index}', '{5}', '{0}', '{0}', '{0}', 0, 0);

	--
	-- Data for Name: bgwriter; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.bgwriter VALUES ($6, 0, 0, 0);
	INSERT INTO statsrepo.bgwriter VALUES ($6 + 1, 600, 6, 60000);
	INSERT INTO statsrepo.bgwriter VALUES ($6 + 2, 2400, 24, 240000);
	INSERT INTO statsrepo.bgwriter VALUES ($6 + 3, 3600, 36, 360000);

	INSERT INTO statsrepo.stat_io (snapid, backend_type, object, context, writes, fsyncs) VALUES ($6 + 0, 'client backend', 'relation', 'normal', 0, 0);
	INSERT INTO statsrepo.stat_io (snapid, backend_type, object, context, writes, fsyncs) VALUES ($6 + 1, 'client backend', 'relation', 'normal', 6000, 60);
	INSERT INTO statsrepo.stat_io (snapid, backend_type, object, context, writes, fsyncs) VALUES ($6 + 2, 'client backend', 'relation', 'normal', 24000, 240);
	INSERT INTO statsrepo.stat_io (snapid, backend_type, object, context, writes, fsyncs) VALUES ($6 + 3, 'client backend', 'relation', 'normal', 36000, 360);

	--
	-- Data for Name: autovacuum_cancel; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.autovacuum_cancel VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_branches', 'ANALYZE-01');
	INSERT INTO statsrepo.autovacuum_cancel VALUES ($1, '2012-11-01 00:00:30+09', 'postgres', 'public', 'pgbench_accounts', 'ANALYZE-01');
	INSERT INTO statsrepo.autovacuum_cancel VALUES ($1, '2012-11-01 00:01:30+09', 'postgres', 'public', 'pgbench_branches', 'ANALYZE-02');

	--
	-- Data for Name: checkpoint; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.checkpoint VALUES ($1, '2012-11-01 00:00:30+09', 'wal', 2283, 0, 0, 0, 44.1259995, 0.191, 45.6969986);
	INSERT INTO statsrepo.checkpoint VALUES ($1, '2012-11-01 00:01:30+09', 'time', 2404, 0, 0, 3, 48.7249985, 0.201000005, 49.1930008);

	--
	-- Data for Name: database; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.database VALUES ($6, 12870, 'postgres', 23524472, 13578, 13530, 0, 7668, 260459, 282319, 29532, 113689, 40434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 109.472999999999999, 20.6739999999999995);
	INSERT INTO statsrepo.database VALUES ($6 + 1, 12870, 'postgres', 24794232, 35815, 35821, 0, 7953, 864774, 579585, 76351, 135957, 107228, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3447.1909999999998, 20.6739999999999995);
	INSERT INTO statsrepo.database VALUES ($6 + 2, 12870, 'postgres', 26047608, 58153, 58207, 0, 8107, 1495879, 876280, 121788, 158311, 174304, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3447.1909999999998, 20.6739999999999995);
	INSERT INTO statsrepo.database VALUES ($6 + 3, 12870, 'postgres', 27366520, 80434, 80500, 0, 8268, 2130801, 1172439, 167065, 180572, 241101, 0, 1, 2, 3, 4, 5, 1, 8388608, 1, 3447.1909999999998, 20.6739999999999995);

	--
	-- Data for Name: cpu; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.cpu VALUES ($6, 'cpu', 22824761, 8286073, 1813481527, 24928112, 0, 0, 0, 0);
	INSERT INTO statsrepo.cpu VALUES ($6 + 1, 'cpu', 22826769, 8286859, 1813481531, 24930644, 0, 0, 0, 0);
	INSERT INTO statsrepo.cpu VALUES ($6 + 2, 'cpu', 22828472, 8287524, 1813481533, 24933783, 0, 0, 0, 0);
	INSERT INTO statsrepo.cpu VALUES ($6 + 3, 'cpu', 22830385, 8288247, 1813481536, 24937040, 0, 0, 0, 0);

	--
	-- Data for Name: device; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.device VALUES ($6, '253', '2', 'dm-2', 221572802, 37740555, 4256657192, 240757474, 0, 278506145, 0, 0, 0, 0, 0, 0, 0, '{pg_default,pg_global}');
	INSERT INTO statsrepo.device VALUES ($6 + 1, '253', '2', 'dm-2', 221573098, 37750828, 4257081528, 241681044, 33, 279451174, 400, 8000, 0, 0, 0, 0, 0, '{pg_default,pg_global}');
	INSERT INTO statsrepo.device VALUES ($6 + 2, '253', '2', 'dm-2', 221573098, 37750828, 4257587368, 245987644, 0, 283746582, 0, 10000, 0, 0, 0, 0, 0, '{pg_default,pg_global}');
	INSERT INTO statsrepo.device VALUES ($6 + 3, '253', '2', 'dm-2', 221573114, 37751872, 4258019304, 247459366, 2, 285219350, 200, 6000, 0, 0, 0, 0, 0, '{pg_default,pg_global}');

	--
	-- Data for Name: function; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.function VALUES ($6, 12870, 16392, 16391, 'sample', '', 10, 0.0899999999999999967, 0.0899999999999999967);
	INSERT INTO statsrepo.function VALUES ($6 + 1, 12870, 16392, 16391, 'sample', '', 22, 0.165000000000000008, 0.165000000000000008);
	INSERT INTO statsrepo.function VALUES ($6 + 2, 12870, 16392, 16391, 'sample', '', 34, 0.262000000000000011, 0.262000000000000011);
	INSERT INTO statsrepo.function VALUES ($6 + 3, 12870, 16392, 16391, 'sample', '', 46, 0.390000000000000013, 0.390000000000000013);

	--
	-- Data for Name: schema; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.schema VALUES ($6, 12870, 11194, 'pg_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6, 12870, 11195, 'pg_toast_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6, 12870, 2200, 'public');
	INSERT INTO statsrepo.schema VALUES ($6, 12870, 16391, 'statsinfo');
	INSERT INTO statsrepo.schema VALUES ($6 + 1, 12870, 11194, 'pg_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 1, 12870, 11195, 'pg_toast_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 1, 12870, 2200, 'public');
	INSERT INTO statsrepo.schema VALUES ($6 + 1, 12870, 16391, 'statsinfo');
	INSERT INTO statsrepo.schema VALUES ($6 + 2, 12870, 11194, 'pg_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 2, 12870, 11195, 'pg_toast_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 2, 12870, 2200, 'public');
	INSERT INTO statsrepo.schema VALUES ($6 + 2, 12870, 16391, 'statsinfo');
	INSERT INTO statsrepo.schema VALUES ($6 + 3, 12870, 11194, 'pg_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 3, 12870, 11195, 'pg_toast_temp_1');
	INSERT INTO statsrepo.schema VALUES ($6 + 3, 12870, 2200, 'public');
	INSERT INTO statsrepo.schema VALUES ($6 + 3, 12870, 16391, 'statsinfo');

	--
	-- Data for Name: table; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.table VALUES ($6, 12870, 16419, 2200, '2012-11-01', 0, 'pgbench_tellers', 0, 0, 'r', 1, 10, '{fillfactor=100}', 49152, 16837, 138210, 0, 0, 10, 13820, 0, 13800, 10, 395, 10, 15, 38352, 3, 22, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.323414+09', NULL, '2012-11-07 19:11:22.324248+09', NULL);
	INSERT INTO statsrepo.table VALUES ($6, 12870, 16425, 2200, '2012-11-01', 0, 'pgbench_history', 0, 0, 'r', 0, 0, NULL, 737280, 0, 0, NULL, NULL, 13820, 0, 0, 0, 13820, 0, 20, 91, 13993, NULL, NULL, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.409937+09', NULL, '2012-11-07 19:11:22.41034+09', NULL);
	INSERT INTO statsrepo.table VALUES ($6, 12870, 16422, 2200, '2012-11-01', 0, 'pgbench_accounts', 0, 0, 'r', 1640, 100000, '{fillfactor=100}', 13688832, 1, 100000, 27640, 27640, 100000, 13820, 0, 12059, 100000, 3404, 30, 6638, 50282, 551, 58704, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.346466+09', NULL, '2012-11-07 19:11:22.405558+09', NULL);
	INSERT INTO statsrepo.table VALUES ($6, 12870, 16416, 2200, '2012-11-01', 0, 'pgbench_branches', 0, 0, 'r', 1, 1, '{fillfactor=100}', 40960, 22600, 13822, 0, 0, 1, 13820, 0, 13813, 1, 391, 40, 14, 101117, 3, 9, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.319834+09', NULL, '2012-11-07 19:11:22.32149+09', NULL);
	INSERT INTO statsrepo.table VALUES ($6 + 1, 12870, 16416, 2200, '2012-11-01', 0, 'pgbench_branches', 0, 0, 'r', 6, 1, '{fillfactor=100}', 49152, 58587, 35740, 0, 0, 1, 35738, 0, 35723, 1, 228, 100, 15, 346003, 3, 20, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.319834+09', '2012-11-07 19:13:16.149766+09', '2012-11-07 19:11:22.32149+09', '2012-11-07 19:13:16.153379+09');
	INSERT INTO statsrepo.table VALUES ($6 + 1, 12870, 16425, 2200, '2012-11-01', 0, 'pgbench_history', 0, 0, 'r', 125, 19511, NULL, 1908736, 0, 0, NULL, NULL, 35738, 0, 0, 0, 35869, 0, 200, 231, 36320, NULL, NULL, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.409937+09', NULL, '2012-11-07 19:11:22.41034+09', '2012-11-07 19:13:17.275153+09');
	INSERT INTO statsrepo.table VALUES ($6 + 1, 12870, 16419, 2200, '2012-11-01', 0, 'pgbench_tellers', 0, 0, 'r', 6, 10, '{fillfactor=100}', 49152, 43591, 357390, 0, 0, 10, 35738, 0, 35718, 10, 320, 300, 15, 207739, 3, 23, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.323414+09', '2012-11-07 19:13:16.166024+09', '2012-11-07 19:11:22.324248+09', '2012-11-07 19:13:16.169689+09');
	INSERT INTO statsrepo.table VALUES ($6 + 1, 12870, 16422, 2200, '2012-11-01', 0, 'pgbench_accounts', 0, 0, 'r', 1672, 100002, '{fillfactor=100}', 13729792, 1, 100000, 71476, 71476, 100000, 35738, 0, 33844, 100002, 3552, 400, 6644, 118458, 551, 146908, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.346466+09', NULL, '2012-11-07 19:11:22.405558+09', '2012-11-07 19:13:17.698361+09');
	INSERT INTO statsrepo.table VALUES ($6 + 2, 12870, 16425, 2200, '2012-11-01', 0, 'pgbench_history', 0, 0, 'r', 269, 41912, NULL, 3063808, 0, 0, NULL, NULL, 58092, 0, 0, 0, 58108, 0, 1000, 376, 59237, NULL, NULL, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.409937+09', NULL, '2012-11-07 19:11:22.41034+09', '2012-11-07 19:14:16.314846+09');
	INSERT INTO statsrepo.table VALUES ($6 + 2, 12870, 16422, 2200, '2012-11-01', 0, 'pgbench_accounts', 0, 0, 'r', 1676, 100002, '{fillfactor=100}', 13795328, 1, 100000, 116184, 116184, 100000, 58092, 0, 55810, 100002, 3927, 2000, 6652, 188689, 551, 237364, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.346466+09', NULL, '2012-11-07 19:11:22.405558+09', '2012-11-07 19:14:17.497618+09');
	INSERT INTO statsrepo.table VALUES ($6 + 2, 12870, 16419, 2200, '2012-11-01', 0, 'pgbench_tellers', 0, 0, 'r', 6, 10, '{fillfactor=100}', 49152, 70764, 580930, 0, 0, 10, 58092, 0, 58072, 10, 500, 3000, 15, 380151, 3, 24, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.323414+09', '2012-11-07 19:14:16.136126+09', '2012-11-07 19:11:22.324248+09', '2012-11-07 19:14:16.141374+09');
	INSERT INTO statsrepo.table VALUES ($6 + 2, 12870, 16416, 2200, '2012-11-01', 0, 'pgbench_branches', 0, 0, 'r', 7, 1, '{fillfactor=100}', 57344, 95251, 58094, 0, 0, 1, 58092, 0, 58072, 1, 135, 4000, 16, 619948, 3, 30, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.319834+09', '2012-11-07 19:14:16.110352+09', '2012-11-07 19:11:22.32149+09', '2012-11-07 19:14:16.123932+09');
	INSERT INTO statsrepo.table VALUES ($6 + 3, 12870, 16422, 2200, '2012-11-01', 0, 'pgbench_accounts', 0, 0, 'r', 1688, 100002, '{fillfactor=100}', 13926400, 1, 100000, 160706, 160706, 100000, 80353, 0, 77145, 100002, 4873, 10000, 6668, 260410, 551, 328518, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.346466+09', NULL, '2012-11-07 19:11:22.405558+09', '2012-11-07 19:15:17.659881+09');
	INSERT INTO statsrepo.table VALUES ($6 + 3, 12870, 16425, 2200, '2012-11-01', 0, 'pgbench_history', 0, 0, 'r', 412, 64164, NULL, 4243456, 0, 0, NULL, NULL, 80353, 0, 0, 0, 80338, 0, 20000, 520, 82202, NULL, NULL, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.409937+09', NULL, '2012-11-07 19:11:22.41034+09', '2012-11-07 19:15:16.442012+09');
	INSERT INTO statsrepo.table VALUES ($6 + 3, 12870, 16419, 2200, '2012-11-01', 0, 'pgbench_tellers', 0, 0, 'r', 6, 10, '{fillfactor=100}', 49152, 97904, 803540, 0, 0, 10, 80354, 0, 80333, 10, 474, 30000, 15, 552040, 3, 25, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.323414+09', '2012-11-07 19:15:16.219036+09', '2012-11-07 19:11:22.324248+09', '2012-11-07 19:15:16.220057+09');
	INSERT INTO statsrepo.table VALUES ($6 + 3, 12870, 16416, 2200, '2012-11-01', 0, 'pgbench_branches', 0, 0, 'r', 7, 1, '{fillfactor=100}', 57344, 131743, 80355, 0, 0, 1, 80355, 0, 80329, 1, 270, 40000, 16, 895948, 3, 38, NULL, NULL, NULL, NULL, '2012-11-07 19:11:22.319834+09', '2012-11-07 19:15:16.159423+09', '2012-11-07 19:11:22.32149+09', '2012-11-07 19:15:16.206658+09');

	--
	-- Data for Name: column; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16416, 1, '2012-11-01', 'bid', 'integer', -1, 'p', true, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16416, 2, '2012-11-01', 'bbalance', 'integer', -1, 'p', false, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16416, 3, '2012-11-01', 'filler', 'character(88)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16419, 1, '2012-11-01', 'tid', 'integer', -1, 'p', true, false, 4, -1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16419, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16419, 3, '2012-11-01', 'tbalance', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16419, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16422, 1, '2012-11-01', 'aid', 'integer', -1, 'p', true, false, 4, -1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16422, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16422, 3, '2012-11-01', 'abalance', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16422, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 85, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 5, '2012-11-01', 'mtime', 'timestamp without time zone', -1, 'p', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 1, '2012-11-01', 'tid', 'integer', -1, 'p', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 4, '2012-11-01', 'delta', 'integer', -1, 'p', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 3, '2012-11-01', 'aid', 'integer', -1, 'p', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 6, '2012-11-01', 'filler', 'character(22)', -1, 'x', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6, 12870, 16425, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, NULL, NULL, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16416, 1, '2012-11-01', 'bid', 'integer', -1, 'p', true, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16416, 2, '2012-11-01', 'bbalance', 'integer', -1, 'p', false, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16416, 3, '2012-11-01', 'filler', 'character(88)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16419, 1, '2012-11-01', 'tid', 'integer', -1, 'p', true, false, 4, -1, -0.433333009);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16419, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16419, 3, '2012-11-01', 'tbalance', 'integer', -1, 'p', false, false, 4, -1, -0.349999994);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16419, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 1, '2012-11-01', 'tid', 'integer', -1, 'p', false, false, 4, 10, 0.0850536004);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 3, '2012-11-01', 'aid', 'integer', -1, 'p', false, false, 4, -0.906309009, 0.0124279);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 4, '2012-11-01', 'delta', 'integer', -1, 'p', false, false, 4, -0.440674007, -0.00160670001);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 5, '2012-11-01', 'mtime', 'timestamp without time zone', -1, 'p', false, false, 8, -1, 0.999302983);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16425, 6, '2012-11-01', 'filler', 'character(22)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16422, 1, '2012-11-01', 'aid', 'integer', -1, 'p', true, false, 4, -1, 0.964172006);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16422, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16422, 3, '2012-11-01', 'abalance', 'integer', -1, 'p', false, false, 4, 4502, 0.670055985);
	INSERT INTO statsrepo.column VALUES ($6 + 1, 12870, 16422, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 85, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16416, 1, '2012-11-01', 'bid', 'integer', -1, 'p', true, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16416, 2, '2012-11-01', 'bbalance', 'integer', -1, 'p', false, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16416, 3, '2012-11-01', 'filler', 'character(88)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16419, 1, '2012-11-01', 'tid', 'integer', -1, 'p', true, false, 4, -1, -0.178571001);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16419, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16419, 3, '2012-11-01', 'tbalance', 'integer', -1, 'p', false, false, 4, -1, -0.178571001);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16419, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 1, '2012-11-01', 'tid', 'integer', -1, 'p', false, false, 4, 10, 0.0913370997);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 3, '2012-11-01', 'aid', 'integer', -1, 'p', false, false, 4, -0.780396998, 0.00216840999);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 4, '2012-11-01', 'delta', 'integer', -1, 'p', false, false, 4, -0.230531007, 0.00335665001);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 5, '2012-11-01', 'mtime', 'timestamp without time zone', -1, 'p', false, false, 8, -1, 0.999872983);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16425, 6, '2012-11-01', 'filler', 'character(22)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16422, 1, '2012-11-01', 'aid', 'integer', -1, 'p', true, false, 4, -1, 0.963553011);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16422, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16422, 3, '2012-11-01', 'abalance', 'integer', -1, 'p', false, false, 4, 7475, 0.431282014);
	INSERT INTO statsrepo.column VALUES ($6 + 2, 12870, 16422, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 85, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16416, 1, '2012-11-01', 'bid', 'integer', -1, 'p', true, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16416, 2, '2012-11-01', 'bbalance', 'integer', -1, 'p', false, false, 4, -1, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16416, 3, '2012-11-01', 'filler', 'character(88)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16419, 1, '2012-11-01', 'tid', 'integer', -1, 'p', true, false, 4, -1, -0.357143015);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16419, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16419, 3, '2012-11-01', 'tbalance', 'integer', -1, 'p', false, false, 4, -1, -0.214286);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16419, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 1, '2012-11-01', 'tid', 'integer', -1, 'p', false, false, 4, 10, 0.0985089019);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 3, '2012-11-01', 'aid', 'integer', -1, 'p', false, false, 4, -0.668365002, 0.00317882001);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 4, '2012-11-01', 'delta', 'integer', -1, 'p', false, false, 4, -0.151939005, 0.00607025018);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 5, '2012-11-01', 'mtime', 'timestamp without time zone', -1, 'p', false, false, 8, -1, 0.999948025);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16425, 6, '2012-11-01', 'filler', 'character(22)', -1, 'x', false, false, 0, 0, NULL);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16422, 1, '2012-11-01', 'aid', 'integer', -1, 'p', true, false, 4, -0.99980998, 0.951070011);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16422, 2, '2012-11-01', 'bid', 'integer', -1, 'p', false, false, 4, 1, 1);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16422, 3, '2012-11-01', 'abalance', 'integer', -1, 'p', false, false, 4, 9313, 0.272940993);
	INSERT INTO statsrepo.column VALUES ($6 + 3, 12870, 16422, 4, '2012-11-01', 'filler', 'character(84)', -1, 'x', false, false, 85, 1, 1);

	--
	-- Data for Name: index; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.index VALUES ($6, 12870, 16429, 16416, '2012-11-01', 0, 'pgbench_branches_pkey', 403, 2, 1, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_branches_pkey ON pgbench_branches USING btree (bid)', 16384, 0, 0, 0, 3, 9);
	INSERT INTO statsrepo.index VALUES ($6, 12870, 16431, 16419, '2012-11-01', 0, 'pgbench_tellers_pkey', 403, 2, 10, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_tellers_pkey ON pgbench_tellers USING btree (tid)', 16384, 0, 0, 0, 3, 22);
	INSERT INTO statsrepo.index VALUES ($6, 12870, 16433, 16422, '2012-11-01', 0, 'pgbench_accounts_pkey', 403, 276, 100000, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_accounts_pkey ON pgbench_accounts USING btree (aid)', 2260992, 27640, 29591, 27640, 551, 58704);
	INSERT INTO statsrepo.index VALUES ($6 + 1, 12870, 16429, 16416, '2012-11-01', 0, 'pgbench_branches_pkey', 403, 2, 3, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_branches_pkey ON pgbench_branches USING btree (bid)', 16384, 0, 0, 0, 3, 20);
	INSERT INTO statsrepo.index VALUES ($6 + 1, 12870, 16431, 16419, '2012-11-01', 0, 'pgbench_tellers_pkey', 403, 2, 10, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_tellers_pkey ON pgbench_tellers USING btree (tid)', 16384, 0, 0, 0, 3, 23);
	INSERT INTO statsrepo.index VALUES ($6 + 1, 12870, 16433, 16422, '2012-11-01', 0, 'pgbench_accounts_pkey', 403, 276, 100002, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_accounts_pkey ON pgbench_accounts USING btree (aid)', 2260992, 71476, 73892, 71476, 551, 146908);
	INSERT INTO statsrepo.index VALUES ($6 + 2, 12870, 16429, 16416, '2012-11-01', 0, 'pgbench_branches_pkey', 403, 2, 1, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_branches_pkey ON pgbench_branches USING btree (bid)', 16384, 0, 0, 0, 3, 30);
	INSERT INTO statsrepo.index VALUES ($6 + 2, 12870, 16431, 16419, '2012-11-01', 0, 'pgbench_tellers_pkey', 403, 2, 10, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_tellers_pkey ON pgbench_tellers USING btree (tid)', 16384, 0, 0, 0, 3, 24);
	INSERT INTO statsrepo.index VALUES ($6 + 2, 12870, 16433, 16422, '2012-11-01', 0, 'pgbench_accounts_pkey', 403, 276, 100002, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_accounts_pkey ON pgbench_accounts USING btree (aid)', 2260992, 116184, 119285, 116184, 551, 237364);
	INSERT INTO statsrepo.index VALUES ($6 + 3, 12870, 16429, 16416, '2012-11-01', 0, 'pgbench_branches_pkey', 403, 2, 2, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_branches_pkey ON pgbench_branches USING btree (bid)', 16384, 0, 0, 0, 3, 38);
	INSERT INTO statsrepo.index VALUES ($6 + 3, 12870, 16431, 16419, '2012-11-01', 0, 'pgbench_tellers_pkey', 403, 2, 10, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_tellers_pkey ON pgbench_tellers USING btree (tid)', 16384, 0, 0, 0, 3, 25);
	INSERT INTO statsrepo.index VALUES ($6 + 3, 12870, 16433, 16422, '2012-11-01', 0, 'pgbench_accounts_pkey', 403, 276, 100002, NULL, true, true, false, true, '1', 'CREATE UNIQUE INDEX pgbench_accounts_pkey ON pgbench_accounts USING btree (aid)', 2260992, 160706, 165141, 160706, 551, 328518);

	--
	-- Data for Name: loadavg; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.loadavg VALUES ($6, 0.50999999, 0.200000003, 0.660000026);
	INSERT INTO statsrepo.loadavg VALUES ($6 + 1, 0.939999998, 0.389999986, 0.689999998);
	INSERT INTO statsrepo.loadavg VALUES ($6 + 2, 1.32000005, 0.600000024, 0.74000001);
	INSERT INTO statsrepo.loadavg VALUES ($6 + 3, 1.20000005, 0.699999988, 0.769999981);

	--
	-- Data for Name: lock; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.lock VALUES ($6 + 1, 12870, NULL, 16416, '', NULL, NULL, -1, 14768, 14771, NULL, 'Lock', 'relation', '00:00:00', 'UPDATE pgbench_branches SET bbalance = bbalance + -3145 WHERE bid = 1;', 'UPDATE pgbench_accounts SET abalance = abalance + -1975 WHERE aid = 65162;SELECT abalance FROM pgbench_accounts WHERE aid = 65162;UPDATE pgbench_tellers SET tbalance = tbalance + -1975 WHERE tid = 4;UPDATE pgbench_branches SET bbalance = bbalance + -1975 WHERE bid = 1;INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (4, 1, 65162, -1975, CURRENT_TI...;');
	INSERT INTO statsrepo.lock VALUES ($6 + 2, 12870, NULL, 16419, '', NULL, NULL, -1, 14769, 14771, NULL, 'Lock', 'relation', '00:00:00', 'UPDATE pgbench_tellers SET tbalance = tbalance + -3715 WHERE tid = 4;', 'UPDATE pgbench_accounts SET abalance = abalance + -1975 WHERE aid = 65162;SELECT abalance FROM pgbench_accounts WHERE aid = 65162;UPDATE pgbench_tellers SET tbalance = tbalance + -1975 WHERE tid = 4;UPDATE pgbench_branches SET bbalance = bbalance + -1975 WHERE bid = 1;INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (4, 1, 65162, -1975, CURRENT_TI...;');

	--
	-- Data for Name: memory; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.memory VALUES ($6, 163992, 25980, 651476, 67440, 184);
	INSERT INTO statsrepo.memory VALUES ($6 + 1, 153676, 26144, 656348, 67440, 28);
	INSERT INTO statsrepo.memory VALUES ($6 + 2, 149148, 26288, 659236, 67440, 5328);
	INSERT INTO statsrepo.memory VALUES ($6 + 3, 143180, 26436, 662056, 67440, 592);

	--
	-- Data for Name: plan; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.plan VALUES ($6, 12870, 10, 847103580, 1970669142, '{"p":{"t":"b","!":"u","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"l":[{"t":"i","h":"m","d":"f","i":"pgbench_accounts_pkey","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"8":"(aid = 46216)"}]}}', 823, 0.110259, 823, 6339, 275, 274, 0, 0, 0, 0, 0, 0, 0, 5.157, 0, 100, 200, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.plan VALUES ($6, 12870, 10, 1067368138, 4132319976, '{"p":{"t":"b","!":"u","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"5":"(bid = 1)"}]}}', 823, 0.0374639999999999, 823, 1647, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 200, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.plan VALUES ($6, 12870, 10, 1899262118, 487339097, '{"p":{"t":"b","!":"u","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"5":"(tid = 2)"}]}}', 823, 0.0412209999999999, 823, 1647, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 200, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 1, 12870, 10, 847103580, 1970669142, '{"p":{"t":"b","!":"u","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"l":[{"t":"i","h":"m","d":"f","i":"pgbench_accounts_pkey","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"8":"(aid = 60907)"}]}}', 1279, 0.153424, 1279, 9383, 288, 287, 0, 0, 0, 0, 0, 0, 0, 5.331, 0, 110, 210, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:01:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 1, 12870, 10, 1067368138, 4132319976, '{"p":{"t":"b","!":"u","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"5":"(bid = 1)"}]}}', 1279, 0.0580179999999998, 1279, 2559, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 210, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:01:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 1, 12870, 10, 1899262118, 487339097, '{"p":{"t":"b","!":"u","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"5":"(tid = 5)"}]}}', 1279, 0.0619969999999996, 1279, 2559, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 210, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:01:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 2, 12870, 10, 847103580, 1970669142, '{"p":{"t":"b","!":"u","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"l":[{"t":"i","h":"m","d":"f","i":"pgbench_accounts_pkey","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"8":"(aid = 65971)"}]}}', 1529, 0.177099, 1529, 10992, 292, 291, 0, 0, 0, 0, 0, 0, 0, 5.37, 0, 120, 220, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:02:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 2, 12870, 10, 1067368138, 4132319976, '{"p":{"t":"b","!":"u","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"5":"(bid = 1)"}]}}', 1529, 0.0735179999999998, 1529, 3059, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 120, 220, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:02:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 2, 12870, 10, 1899262118, 487339097, '{"p":{"t":"b","!":"u","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"5":"(tid = 1)"}]}}', 1529, 0.0729339999999998, 1529, 3059, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 120, 220, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:02:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 3, 12870, 10, 847103580, 1970669142, '{"p":{"t":"b","!":"u","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"l":[{"t":"i","h":"m","d":"f","i":"pgbench_accounts_pkey","n":"pgbench_accounts","a":"pgbench_accounts","1":0.29,"2":8.31,"3":1,"4":103,"8":"(aid = 9747)"}]}}', 1897, 0.204121, 1897, 13129, 294, 293, 0, 0, 0, 0, 0, 0, 0, 5.37, 0, 130, 230, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:03:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 3, 12870, 10, 1067368138, 4132319976, '{"p":{"t":"b","!":"u","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_branches","a":"pgbench_branches","1":0.00,"2":1.01,"3":1,"4":106,"5":"(bid = 1)"}]}}', 1896, 0.0980150000000002, 1896, 3793, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 130, 230, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:03:00+09');
	INSERT INTO statsrepo.plan VALUES ($6 + 3, 12870, 10, 1899262118, 487339097, '{"p":{"t":"b","!":"u","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"l":[{"t":"h","h":"m","n":"pgbench_tellers","a":"pgbench_tellers","1":0.00,"2":1.13,"3":1,"4":106,"5":"(tid = 6)"}]}}', 1897, 0.0897130000000002, 1897, 3795, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 130, 230, 0, 0, '2012-11-01 00:00:00+09', '2012-11-01 00:03:00+09');

	--
	-- Data for Name: replication; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.replication VALUES ($6, 14171, 10, 'postgres', 'walreceiver', '127.0.0.1', '', 58145, '2012-11-01 00:00:00+09', '100', 'streaming', '0/30206D8 (000000010000000000000003)', '0/30206D8 (000000010000000000000003)', '0/3000000 (000000010000000000000002)', '0/3000000 (000000010000000000000002)', '0/3000000 (000000010000000000000002)', '00:00:01', '00:00:02', '00:00:03', 0, 'async');
	INSERT INTO statsrepo.replication VALUES ($6 + 1, 14171, 10, 'postgres', 'walreceiver', '127.0.0.1', '', 58145, '2012-11-01 00:00:00+09', '200', 'streaming', '0/45E6680 (000000010000000000000004)', '0/45E6680 (000000010000000000000004)', '0/45E6680 (000000010000000000000004)', '0/45E6680 (000000010000000000000004)', '0/45E63A0 (000000010000000000000004)', '00:00:04', '00:00:05', '00:00:06', 0, 'async');
	INSERT INTO statsrepo.replication VALUES ($6 + 2, 14171, 10, 'postgres', 'walreceiver', '127.0.0.1', '', 58145, '2012-11-01 00:00:00+09', '300', 'streaming', '0/5402F60 (000000010000000000000005)', '0/5402F60 (000000010000000000000005)', '0/5402F60 (000000010000000000000005)', '0/5400DE8 (000000010000000000000005)', '0/5400DE8 (000000010000000000000005)', '00:00:07', '00:00:08', '00:00:09', 0, 'async');
	INSERT INTO statsrepo.replication VALUES ($6 + 3, 14171, 10, 'postgres', 'walreceiver', '127.0.0.1', '', 58145, '2012-11-01 00:00:00+09', '400', 'streaming', '0/685E818 (000000010000000000000006)', '0/685E818 (000000010000000000000006)', '0/685E818 (000000010000000000000006)', '0/685E578 (000000010000000000000006)', '0/685E578 (000000010000000000000006)', '00:00:10', '00:00:11', '00:00:12', 0, 'async');

	--
	-- Data for Name: replication_slots; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.replication_slots VALUES ($6    , 'mysub', 'pgoutput', 'logical', 12870, false, true, 22279, NULL, '742', '0/1B39C20', '0/2C0F4D0');
	INSERT INTO statsrepo.replication_slots VALUES ($6    , 'mysub2', 'pgoutput', 'logical', 12870, false, false, NULL, NULL, '742', '0/1B39C20', '0/1B39C58');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 1, 'mysub', 'pgoutput', 'logical', 12870, false, true, 22279, NULL, '749', '0/2D37028', '0/2D37060');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 1, 'mysub2', 'pgoutput', 'logical', 12870, false, true, 22295, NULL, '749', '0/2D37028', '0/2D37060');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 2, 'mysub', 'pgoutput', 'logical', 12870, false, true, 22279, NULL, '749', '0/2D37028', '0/4277E98');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 2, 'mysub2', 'pgoutput', 'logical', 12870, false, true, 22295, NULL, '749', '0/2D37028', '0/4277E98');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 3, 'mysub', 'pgoutput', 'logical', 12870, false, true, 22279, NULL, '766', '0/437C680', '0/437C6B8');
	INSERT INTO statsrepo.replication_slots VALUES ($6 + 3, 'mysub2', 'pgoutput', 'logical', 12870, false, true, 22295, NULL, '766', '0/437C680', '0/437C6B8');

	--
	-- Data for Name: stat_replication_slots; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6    , 'mysub', 2, 486, 31800000, 0, 0, 0, 2, 31800000, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6 + 1, 'mysub', 2, 365, 23850000, 0, 0, 0, 2, 23850000, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6 + 2, 'mysub', 2, 607, 39750000, 0, 0, 0, 2, 39750000, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6 + 2, 'mysub2', 0, 0, 0, 2, 607, 39750000, 2, 39750000, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6 + 3, 'mysub', 4, 730, 47700000, 0, 0, 0, 4, 47700000, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_replication_slots VALUES ($6 + 3, 'mysub2', 0, 0, 0, 4, 730, 47700000, 4, 47700000, '2012-11-01 00:00:00+09');

	--
	-- Data for Name: role; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.role VALUES ($6, 10, 'postgres');
	INSERT INTO statsrepo.role VALUES ($6 + 1, 10, 'postgres');
	INSERT INTO statsrepo.role VALUES ($6 + 2, 10, 'postgres');
	INSERT INTO statsrepo.role VALUES ($6 + 3, 10, 'postgres');

	--
	-- Data for Name: setting; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.setting VALUES ($6, 'shared_buffers', '4096', '8kB', 'configuration file');
	INSERT INTO statsrepo.setting VALUES ($6 + 1, 'shared_buffers', '4096', '8kB', 'configuration file');
	INSERT INTO statsrepo.setting VALUES ($6 + 2, 'shared_buffers', '4096', '8kB', 'configuration file');
	INSERT INTO statsrepo.setting VALUES ($6 + 3, 'shared_buffers', '4096', '8kB', 'configuration file');

	--
	-- Data for Name: statement; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.statement VALUES ($6, 12870, 10, 1067368138, 'UPDATE pgbench_branches SET bbalance = bbalance + ? WHERE bid = ?;', 0,0, 13959, 68.6447329999982969, 13959, 102552, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0.0749999999999999972, 0, 100.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6, 12870, 10, 1899262118, 'UPDATE pgbench_tellers SET tbalance = tbalance + ? WHERE tid = ?;', 0,0, 13961, 17.1652659999997006, 13961, 39461, 12, 11, 0, 0, 0, 0, 0, 0, 0, 0.0889999999999999958, 0, 200.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6, 12870, 10, 847103580, 'UPDATE pgbench_accounts SET abalance = abalance + ? WHERE aid = ?;', 0,0, 13962, 1.42280999999985003, 13962, 63130, 1950, 1948, 0, 0, 0, 0, 0, 0, 0, 73.9310000000001963, 0, 300.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 1, 12870, 10, 1067368138, 'UPDATE pgbench_branches SET bbalance = bbalance + ? WHERE bid = ?;', 0,0, 36061, 176.479407999995004, 36061, 349687, 11, 11, 0, 0, 0, 0, 0, 0, 0, 0.0749999999999999972, 0, 100.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 1, 12870, 10, 1899262118, 'UPDATE pgbench_tellers SET tbalance = tbalance + ? WHERE tid = ?;', 0,0, 36063, 45.5574849999972997, 36063, 210248, 12, 11, 0, 0, 0, 0, 0, 0, 0, 0.0889999999999999958, 0, 200.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 1, 12870, 10, 847103580, 'UPDATE pgbench_accounts SET abalance = abalance + ? WHERE aid = ?;', 0,0, 36064, 3.11938400000004012, 36064, 152482, 1956, 1954, 0, 0, 0, 0, 0, 0, 0, 73.9570000000002068, 0, 300.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 2, 12870, 10, 1067368138, 'UPDATE pgbench_branches SET bbalance = bbalance + ? WHERE bid = ?;', 0,0, 58140, 286.760816999982978, 58140, 620495, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0.0749999999999999972, 0, 100.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 2, 12870, 10, 1899262118, 'UPDATE pgbench_tellers SET tbalance = tbalance + ? WHERE tid = ?;', 0,0, 58143, 73.9565750000011946, 58143, 380498, 12, 13, 0, 0, 0, 0, 0, 0, 0, 0.0889999999999999958, 0, 200.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 2, 12870, 10, 847103580, 'UPDATE pgbench_accounts SET abalance = abalance + ? WHERE aid = ?;', 0,0, 58143, 5.1055540000000299, 58143, 242773, 1964, 2279, 0, 0, 0, 0, 0, 0, 0, 73.9570000000002068, 0, 300.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 3, 12870, 10, 1067368138, 'UPDATE pgbench_branches SET bbalance = bbalance + ? WHERE bid = ?;', 0,0, 80422, 400.019771999942975, 80422, 896817, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0.0749999999999999972, 0, 100.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 3, 12870, 10, 1899262118, 'UPDATE pgbench_tellers SET tbalance = tbalance + ? WHERE tid = ?;', 0,0, 80424, 102.535575000004997, 80424, 552512, 12, 13, 0, 0, 0, 0, 0, 0, 0, 0.0889999999999999958, 0, 200.0, 50);
	INSERT INTO statsrepo.statement VALUES ($6 + 3, 12870, 10, 847103580, 'UPDATE pgbench_accounts SET abalance = abalance + ? WHERE aid = ?;', 0,0, 80425, 6.97668499999906988, 80425, 336204, 1980, 2332, 0, 0, 0, 0, 0, 0, 0, 73.9570000000002068, 0, 300.0, 50);

	--
	-- Data for Name: tablespace; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.tablespace VALUES ($6, 1663, 'pg_default', '/var/pgdata', '253:2', 27446407168, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6, 1664, 'pg_global', '/var/pgdata', '253:2', 27446407168, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 1, 1663, 'pg_default', '/var/pgdata', '253:2', 27427938304, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 1, 1664, 'pg_global', '/var/pgdata', '253:2', 27427938304, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 2, 1663, 'pg_default', '/var/pgdata', '253:2', 27392835584, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 2, 1664, 'pg_global', '/var/pgdata', '253:2', 27392835584, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 3, 1663, 'pg_default', '/var/pgdata', '253:2', 27374673920, 51288186880, NULL);
	INSERT INTO statsrepo.tablespace VALUES ($6 + 3, 1664, 'pg_global', '/var/pgdata', '253:2', 27374673920, 51288186880, NULL);

	--
	-- Data for Name: xact; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.xact VALUES ($6, '127.0.0.1', 10590, '2012-11-01 00:00:00+09', 1, 'SELECT 1');
	INSERT INTO statsrepo.xact VALUES ($6 + 1, '127.0.0.1', 10591, '2012-11-01 00:00:30+09', 2, 'SELECT 2');
	INSERT INTO statsrepo.xact VALUES ($6 + 2, '127.0.0.1', 10592, '2012-11-01 00:01:30+09', 3, 'SELECT 3');
	INSERT INTO statsrepo.xact VALUES ($6 + 3, '127.0.0.1', 10593, '2012-11-01 00:02:30+09', 4, 'SELECT 4');

	--
	-- Data for Name: xlog; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.xlog VALUES ($6, '0/2EFAF90', '000000010000000000000002');
	INSERT INTO statsrepo.xlog VALUES ($6 + 1, '0/3B7C140', '000000010000000000000003');
	INSERT INTO statsrepo.xlog VALUES ($6 + 2, '0/5635000', '000000010000000000000005');
	INSERT INTO statsrepo.xlog VALUES ($6 + 3, '0/63614A8', '000000010000000000000006');

	--
	-- Data for Name: stat_wal; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.stat_wal VALUES ($6    , 4320, 504, 4451688, 0, 146, 0, 0, 0, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_wal VALUES ($6 + 1, 218663, 982, 24563492, 338, 521, 0, 0, 0, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_wal VALUES ($6 + 2, 827323, 983, 68873779, 1876, 2085, 0, 0, 0, '2012-11-01 00:00:00+09');
	INSERT INTO statsrepo.stat_wal VALUES ($6 + 3, 828860, 983, 69090158, 1876, 2096, 0, 0, 0, '2012-11-01 00:00:00+09');

	--
	-- Data for Name: wait_sampling; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.wait_sampling VALUES ($6, 12870, 10, 1067368138, 'client backend', 'IO', 'DataFileRead', 2);
	INSERT INTO statsrepo.wait_sampling VALUES ($6, 12870, 10, 1067368138, 'client backend', 'Client', 'ClientRead', 1);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 1, 12870, 10, 1067368138, 'client backend', 'IO', 'DataFileRead', 4);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 1, 12870, 10, 1067368138, 'client backend', 'Client', 'ClientRead', 2);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 2, 12870, 10, 1067368138, 'client backend', 'IO', 'DataFileRead', 8);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 2, 12870, 10, 1067368138, 'client backend', 'Client', 'ClientRead', 4);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 3, 12870, 10, 1067368138, 'client backend', 'IO', 'DataFileRead', 16);
	INSERT INTO statsrepo.wait_sampling VALUES ($6 + 3, 12870, 10, 1067368138, 'client backend', 'Client', 'ClientRead', 8);

	--
	-- Data for Name: cpuinfo; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.cpuinfo VALUES ($1, '2012-10-31 00:00:00+09', 'CPU_vendor01', 'CPU_model_name', 3191.998, 4, 1, 4, 1);
	INSERT INTO statsrepo.cpuinfo VALUES ($1, '2012-11-01 00:01:00+09', 'CPU_vendor01', 'CPU_model_name', 3191.998, 2, 1, 2, 1);
	INSERT INTO statsrepo.cpuinfo VALUES ($1, '2012-11-01 00:02:00+09', 'CPU_vendor01', 'CPU_model_name', 3191.998, 4, 1, 4, 1);

	--
	-- Data for Name: meminfo; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.meminfo VALUES ($1, '2012-10-31 00:00:00+09', 3917156352);
	INSERT INTO statsrepo.meminfo VALUES ($1, '2012-11-01 00:01:00+09', 1904697344);
	INSERT INTO statsrepo.meminfo VALUES ($1, '2012-11-01 00:02:00+09', 3917156352);

	--
	-- Data for Name: rusage; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.rusage VALUES ($6, 12870, 10, 1067368138, 1, 1, 0.1, 0.1, 1, 1, 1, 1, 2, 2, 0.2, 0.2, 2, 2, 2, 2);
	INSERT INTO statsrepo.rusage VALUES ($6, 12870, 10, 1899262118, 1, 1, 0.1, 0.1, 1, 1, 1, 1, 2, 2, 0.2, 0.2, 2, 2, 2, 2);
	INSERT INTO statsrepo.rusage VALUES ($6, 12870, 10, 847103580,  1, 1, 0.1, 0.1, 1, 1, 1, 1, 2, 2, 0.2, 0.2, 2, 2, 2, 2);
	INSERT INTO statsrepo.rusage VALUES ($6 + 1, 12870, 10, 1067368138, 2, 2, 0.2, 0.2, 2, 2, 2, 2, 4, 4, 0.4, 0.4, 4, 4, 4, 4);
	INSERT INTO statsrepo.rusage VALUES ($6 + 1, 12870, 10, 1899262118, 2, 2, 0.2, 0.2, 2, 2, 2, 2, 4, 4, 0.4, 0.4, 4, 4, 4, 4);
	INSERT INTO statsrepo.rusage VALUES ($6 + 1, 12870, 10, 847103580,  2, 2, 0.2, 0.2, 2, 2, 2, 2, 4, 4, 0.4, 0.4, 4, 4, 4, 4);
	INSERT INTO statsrepo.rusage VALUES ($6 + 2, 12870, 10, 1067368138, 3, 3, 0.3, 0.3, 3, 3, 3, 3, 6, 6, 0.6, 0.6, 6, 6, 6, 6);
	INSERT INTO statsrepo.rusage VALUES ($6 + 2, 12870, 10, 1899262118, 3, 3, 0.3, 0.3, 3, 3, 3, 3, 6, 6, 0.6, 0.6, 6, 6, 6, 6);
	INSERT INTO statsrepo.rusage VALUES ($6 + 2, 12870, 10, 847103580,  3, 3, 0.3, 0.3, 3, 3, 3, 3, 6, 6, 0.6, 0.6, 6, 6, 6, 6);
	INSERT INTO statsrepo.rusage VALUES ($6 + 3, 12870, 10, 1067368138, 4, 4, 0.4, 0.4, 4, 4, 4, 4, 8, 8, 0.8, 0.8, 8, 8, 8, 8);
	INSERT INTO statsrepo.rusage VALUES ($6 + 3, 12870, 10, 1899262118, 4, 4, 0.4, 0.4, 4, 4, 4, 4, 8, 8, 0.8, 0.8, 8, 8, 8, 8);
	INSERT INTO statsrepo.rusage VALUES ($6 + 3, 12870, 10, 847103580,  4, 4, 0.4, 0.4, 4, 4, 4, 4, 8, 8, 0.8, 0.8, 8, 8, 8, 8);

	--
	-- Data for Name: ht_info; Type: TABLE DATA; Schema: statsrepo; Owner: postgres
	--
	INSERT INTO statsrepo.ht_info VALUES ($6, 0, '2012-10-31 00:00:00+09', 0, '2012-10-31 00:00:00+09', 0, '2012-10-31 00:00:00+09');
	INSERT INTO statsrepo.ht_info VALUES ($6 + 1, 10, '2012-11-01 01:00:00+09', 10, '2012-11-01 01:00:00+09', 10, '2012-11-01 01:00:00+09');
	INSERT INTO statsrepo.ht_info VALUES ($6 + 2, 20, '2012-11-01 02:00:00+09', 20, '2012-11-01 02:00:00+09', 20, '2012-11-01 02:00:00+09');
	INSERT INTO statsrepo.ht_info VALUES ($6 + 3, 30, '2012-11-01 03:00:00+09', 30, '2012-11-01 03:00:00+09', 30, '2012-11-01 03:00:00+09');

$$ LANGUAGE sql;
