# Troubleshooting

## Linux: "Device or resource busy" error

On some Linux distributions ALSA playback device can't be opened because PulseAudio is holding it.  To fix this, configure fmedia to use PulseAudio as a default playback device:

1. in `fmedia.conf` find this line:

	# output "pulse.out"

2. and uncomment it:

	output "pulse.out"

3. That's all!  Now fmedia will use PulseAudio device.
