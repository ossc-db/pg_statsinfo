/*
 * lib/rusage.h
 *
 * Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */


typedef enum
{
	STATSINFO_RUSAGE_TRACK_NONE,	/* track no statements */
	STATSINFO_RUSAGE_TRACK_TOP,	 /* only top level statements */
	STATSINFO_RUSAGE_TRACK_ALL	  /* all statements, including nested ones */
}   STATSINFO_RUSAGE_TrackLevel;

static const struct config_enum_entry rusage_track_options[] =
{
	{"none", STATSINFO_RUSAGE_TRACK_NONE, false},
	{"top", STATSINFO_RUSAGE_TRACK_TOP, false},
	{"all", STATSINFO_RUSAGE_TRACK_ALL, false},
	{NULL, 0, false}
};
