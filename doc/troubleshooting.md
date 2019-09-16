# Troubleshooting

* Windows: Recording or Playback doesn't work


## Windows: Recording or Playback doesn't work

Most likely it means that fmedia couldn't agree with WASAPI about the audio format to use.  You can try to switch to Direct Sound - it's a more robust technology for audio I/O.  Note that it may lower your sound quality due to unnecessary audio conversion.

For playback, do:

1. in `fmedia.conf` find this:

		output "wasapi.out"
		# output "direct-sound.out"

2. and change it to:

		# output "wasapi.out"
		output "direct-sound.out"

For recording, do:

1. in `fmedia.conf` find this:

		input "wasapi.in"
		# input "direct-sound.in"

2. and change it to:

		# input "wasapi.in"
		input "direct-sound.in"

Now fmedia will use Direct Sound.
