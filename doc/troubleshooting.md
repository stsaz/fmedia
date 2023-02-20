# Troubleshooting

* Windows: Recording fails with "Access is denied" error
* Windows: Recording or Playback doesn't work


## Windows: Recording fails with "Access is denied" error

If fmedia fails with this error when starting audio recording:

	open device #0: IAudioClient_Initialize: -2147024891 (0x80070005) Access is denied

It's probably because Windows denies access to your microphone device.  Here's how to fix it:

1. Open `Microphone Privacy Settings` window.
2. Enable `Allow apps to access your microphone` checkbox.
3. Enable `Allow desktop apps to access your microphone` checkbox.

More info and screenshots are here: https://github.com/stsaz/fmedia/issues/71#issuecomment-1115407629


## Windows: Recording or Playback doesn't work

Most likely it means that fmedia couldn't agree with WASAPI about the audio format to use.  You can try to switch to Direct Sound - it's a more robust technology for audio I/O.  Note that it may lower your sound quality due to unnecessary audio conversion.

For playback, do:

1. in `fmedia.conf` find this:

		output "wasapi.out"
		output "direct-sound.out"

2. and change it to:

		output "direct-sound.out"
		output "wasapi.out"

For recording, do:

1. in `fmedia.conf` find this:

		input "wasapi.in"
		input "direct-sound.in"

2. and change it to:

		input "direct-sound.in"
		input "wasapi.in"

Now fmedia will use Direct Sound.
