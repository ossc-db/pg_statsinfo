## pg_statsinfo 14.0-14.2 から 14.4 以降へのアップデートに関する注意

通常、マイナーバージョンアップではリポジトリを継続して使用できますが、pg_statsinfo 14.0, 14.1, 14.2から14.4以降にマイナーバージョンアップを行う場合は、リポジトリの再初期化が必要です（14.3からのアップデートでは再初期化は不要です）。再初期化を行わずにpg_statsinfoを起動すると、フォールバックモードに移行し、本来の動作が行われなくなりますのでご注意ください。


## Important Notice for Upgrading from pg_statsinfo 14.0-14.2 to 14.4 or Later

In typical minor version upgrades, repositories remain usable with their original schema. However, when upgrading pg_statsinfo from version from 14.0 through 14.2 to 14.4 or later, the repository must be reinitialized (reinitialization is not required when upgrading from 14.3). If you attempt to start pg_statsinfo without reinitializing the repository, it will enter fallback mode and will not perform its intended operations. Please take note of this important requirement.

-----
Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
