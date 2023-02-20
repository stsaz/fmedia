# Tips & Tricks

* How to properly set up portable mode
* Linux: use ALSA rather than Pulse Audio

## How to properly set up portable mode

Portable mode is when fmedia doesn't use any external directories (such as %APPDATA%) for per-user configuration.  Instead, it uses only the application directory you installed fmedia into.  You can copy its directory to a flash drive and it will run from there.

You can edit `fmedia.conf` file and set `portable_conf true` setting, but this change should be done after each upgrade.  Below is the more convenient method:

1. Create file `fmedia-ext.conf` in application directory with these contents:

		core.portable_conf true

By using `fmedia-ext.conf` configuration file you won't need to edit `fmedia.conf` after fmedia upgrade.


## Linux: use ALSA rather than Pulse Audio for playback

If you suspect that Pulse Audio is not the best choice for audio playback due to sound quality, latency or performance, you can configure fmedia to use ALSA:

1. in `fmedia.conf` find these lines:

		output "pulse.out"
		output "alsa.out"

2. and change them to:

		output "alsa.out"
		output "pulse.out"

Now fmedia will use ALSA for audio playback.
