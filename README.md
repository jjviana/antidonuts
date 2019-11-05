# Antidonuts

This program will monitor your Mac for input (keyboard and mouse activity). 
As soon as it detects the computer has been idle for a configurable number of seconds,
it will take a picture using the webcam and use Computer Vision to check if there is a face
looking at the computer. If it cannot detect a face it will immediately put the display to sleep
(which, if the settings are configured correctly, should cause the display to lock).

## How to Install

1. Install Go
2. Install Homebrew
3. Install GoCV package: `go get -u -d gocv.io/x/gocv`
4. Install OpenCV: `brew install opencv`
5. Install PkgConfig: `brew install pkgconfig`
6. Clone this repository: `git clone https://github.com/jjviana/antidonuts.git`
7. Build: `cd antidonuts; go build .`
8. Run: `./antidonuts --help`


