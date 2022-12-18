# fmedia automatic builder for Fedora/Debian/macOS
# 2022, Simon Zolin

echo 'This script will automatically DOWNLOAD and BUILD fmedia on your computer.
Note that your fmedia app package may contain new features (or bugs!)
 which are not officially released yet.
You will see everything that is happening at the moment
 and you can abort at any time by pressing Ctrl+C.
Press Enter to continue;  press Ctrl+C to abort.'
read

MAKE='make -j8 -Rr'

set -xe

echo 'Installing build tools and libs...'

OS=$(uname)
if test "$OS" == "Linux" ; then
	DNF=$(which dnf)
	if test "$DNF" != "" ; then
		sudo dnf install git make cmake gcc gcc-c++ patch dos2unix curl \
			gtk3-devel dbus-devel \
			alsa-lib-devel pulseaudio-libs-devel pipewire-jack-audio-connection-kit-devel
	else
		sudo apt install git make cmake gcc g++ patch dos2unix curl \
			libgtk-3-dev libdbus-1-dev \
			libasound2-dev libpulse-dev libjack-dev
	fi

elif test "$OS" == "Darwin" ; then
	brew install git
	brew install make
	brew install cmake
	brew install llvm
	brew install dos2unix
fi

mkdir -p fmedia-src
cd fmedia-src


echo 'Copying repositories...'

git clone https://github.com/stsaz/ffbase
git clone https://github.com/stsaz/ffaudio
git clone https://github.com/stsaz/ffos
git clone https://github.com/stsaz/avpack
git clone https://github.com/stsaz/fmedia


echo 'Building third-party audio codecs (alib3)...'

cd fmedia/alib3
$MAKE
if test "$OS" == "Linux" ; then
	$MAKE md5check
fi
$MAKE install
cd ../../


echo 'Building the app...'

cd fmedia
$MAKE install
cd ..

echo 'Checking...'
fmedia/fmedia-1/fmedia

echo 'READY!  Now you may copy fmedia/fmedia-1 directory anywhere you want.'
