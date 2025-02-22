# ufs2tools-reboot

これは、UFSパーティションをWindowsで読み込むことができるツールである ufs2tools ([HP](https://ufs2tools.sourceforge.net/), [SourceForge](https://sourceforge.net/projects/ufs2tools/)) のバージョン0.8を、とりあえずVisual Studio 2022でビルドできるようにしました。

私はC/C++を用いた開発の経験がほとんど無いため、おそらくその開発の慣例には従えていないと思いますが、ご容赦ください。

## 注意

どうやらWindows Vista以降、このツールの利用には管理者権限が必要なようです。([参照](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea#physical-disks-and-volumes))  
また、残念ながら私はディスクのデータ構造に詳しくないため、これは推測に過ぎないものの、このツールはGPTパーティションを読み込めないようです。ご注意ください。

## 付録

以下、フォーク元のプロジェクトのREADMEを記します。

Compilation
-----------

To compile with mingw, type 'make'.
To compile with msvc, open up the ufs2tools.dsw workspace file.

Usage
-----

ufstool drive[/slice]/partition [-lg] srcpath [destpath]
NOTE: drive and partition are 0-based, slice is 1-based

examples:

To list files in /usr/bin on /dev/ad1s2a

    ufs2tool 1/2/0 usr/bin

To copy file usr/include/string.h to s.h

    ufs2tool 1/2/0 -g usr/include/string.h s.h

To copy file usr/include/string.h to stdout

    ufs2tool 1/2/0 -g usr/include/string.h CON

To retrieve the /var/log directory recursively to ./log

    ufs2tool 1/2/0 -g /var/log

Destination Directory Behaviour
-------------------------------

For the following:

    ufs2tool 1/2/0 -g /var/log destdir
    
If the directory 'destdir' exists, the 'log' directory will
be copied into 'destdir'.

If 'destdir' doesn't exist, it will be created, and the
contents of the 'log' directory (rather than the 'log'
directory itself) will be copied into 'destdir'.

Notes / Caveats
---------------

- For filenames that are valid for ufs, but not for ntfs/fat,
(for example, 'prn.txt', 'x::y'), the filename will be changed
accordingly and '__' will be prepended.

- If the destination file already exists, it will be overwritten.
This means that if a directory has files named 'TEST' and 'test',
only the latter will be copied over.

- In a 'sh' environment (such as msys/cygwin), you may need to
prefix any leading '/' characters with a '.' character, eg:

    ufs2tool 1/2/0 -g ./var/log

Todo
----

- write support

Changes
-------

0.8
- fix 32-bit truncation on UFS1
- code cleanups

0.7
- msvc support
- better error checking
- allow copying to stdout
- preserve timestamps
- code cleanups

0.6
- add ufs1 support
- correctly search for superblock
- fix some incorrect wording
- code cleanups

0.5
- ignore alternate file streams
- can now use directory as destination path
- optimized transfer speed
- code cleanups
- bug fixes for windows filenames

0.4
- skip looping symlinks
- workaround for invalid windows filenames
- memory bugs fixed
- ufs2tool/bsdlabel correctly opens specified device
- code cleanups
- memory leaks fixed

0.3
- support sparse files
- symlink bugs fixed
- buffer overflow fixed
- clean up user interface

0.2
- fixed retrieving symlinks
- can retrieve directories recursively

0.1
- initial release
