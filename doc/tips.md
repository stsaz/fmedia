# Tips & Tricks

## How to properly set up portable mode

Portable mode is when fmedia doesn't use any external directories (such as %APPDATA%) for per-user configuration.  Instead, it uses only the application directory you installed fmedia into.  You can copy its directory to a flash drive and it will run from there.

You can edit `fmedia.conf` file and set `portable_conf true` setting, but this change should be done after each upgrade.  Below is the more convenient method:

1. Create file `fmedia-ext.conf` in application directory with these contents:

	core.portable_conf true

By using `fmedia-ext.conf` configuration file you won't need to edit `fmedia.conf` after fmedia upgrade.
