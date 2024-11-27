## pg_statsinfo 15.0/15.1 から 15.3 以降へのアップデートに関する注意

通常、マイナーバージョンアップではリポジトリを継続して使用できますが、pg_statsinfo 15.0および15.1から15.3以降にマイナーバージョンアップを行う場合は、リポジトリの再初期化が必要です（15.2からのアップデートでは再初期化は不要です）。再初期化を行わずにpg_statsinfoを起動すると、フォールバックモードに移行し、本来の動作が行われなくなりますのでご注意ください。


## Important Notice for Upgrading from pg_statsinfo 15.0/15.1 to 15.3 or Later

In typical minor version upgrades, repositories remain usable with their original schema. However, when upgrading pg_statsinfo from version 15.0 or 15.1 to 15.3 or later, the repository must be reinitialized (reinitialization is not required when upgrading from 15.2). If you attempt to start pg_statsinfo without reinitializing the repository, it will enter fallback mode and will not perform its intended operations. Please take note of this important requirement.

-----
Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
