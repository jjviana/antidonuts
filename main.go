// Heavily based on the gocv face detector example

package main

// #cgo LDFLAGS: -framework CoreFoundation -framework IOKit
// void onSystemIdle();
// void onDisplaySleep();
// void onDisplayWakeup();
// #include "sleepwatcher.h"
import "C"
import (
	"flag"
	"image"
	"image/color"
	"log"
	"os/exec"
	"time"

	"gocv.io/x/gocv"
)

type detectedFace struct {
	confidence float32
	top        int
	bottom     int
	left       int
	right      int
}

var net gocv.Net
var ratio float64
var mean gocv.Scalar
var swapRGB bool
var window *gocv.Window = nil
var img gocv.Mat
var displaySleeping int
var faceCoveragePercentPtr *float64
var faceDetectionTreshold *float64
var idleTimePtr *int
var debug *bool

func main() {

	idleTimePtr = flag.Int("idleCheck", 10, "Idle time in seconds before visual check")
	faceCoveragePercentPtr = flag.Float64("minFaceAreaPercent", 5, "Minumum screen area (in %) that needs to be covered by face")
	faceDetectionTreshold = flag.Float64("faceDetectionTreshold", 25, "Treshold probability for face detection (in %)")
	debug = flag.Bool("debug", false, "Show window with face detection information")

	flag.Parse()

	*faceCoveragePercentPtr = *faceCoveragePercentPtr / 100
	*faceDetectionTreshold = *faceDetectionTreshold / 100

	model := "res10_300x300_ssd_iter_140000.caffemodel"
	config := "deploy.prototxt"

	// open DNN object tracking model
	net = gocv.ReadNet(model, config)
	if net.Empty() {
		log.Printf("Error reading neural network model from : %v %v\n", model, config)
		net.Close()
		return
	}

	net.SetPreferableBackend(gocv.NetBackendType(gocv.NetBackendDefault))
	net.SetPreferableTarget(gocv.NetTargetType(gocv.NetTargetCPU))

	ratio = 1.0
	mean = gocv.NewScalar(104, 177, 123, 0)
	swapRGB = false

	// prepare image matrix
	img = gocv.NewMat()

	C.setupSleepWatcher(C.int(10 * *idleTimePtr))
}

//export onDisplaySleep
func onDisplaySleep() {
	displaySleeping = 1
}

//export onDisplayWakeup
func onDisplayWakeup() {
	displaySleeping = 0
}

//export onSystemIdle
func onSystemIdle() {
	log.Println("I'm idle!!!!!!")

	if displaySleeping == 1 {
		log.Println("But display is sleeping...")
		return
	}

	// parse args
	deviceID := 0

	if *debug && window == nil {
		window = gocv.NewWindow("DNN Detection")
	}

	log.Printf("Start reading device: %v\n", deviceID)

	if ok := readImage(deviceID, &img); !ok {
		log.Println("Cannot read from device - too dark? Sleeping display anyway")
		sleepDisplay()
		return

	}

	// detect faces
	numDetectedFaces := 0

	// convert image Mat to 300x300 blob that the object detector can analyze
	blob := gocv.BlobFromImage(img, ratio, image.Pt(300, 300), mean, swapRGB, false)

	// feed the blob into the detector
	net.SetInput(blob, "")

	// run a forward pass thru the network
	prob := net.Forward("")
	faces := detectFaces(&img, prob)

	prob.Close()
	blob.Close()

	imgArea := float32(img.Size()[0] * img.Size()[1])

	for ind, face := range faces {
		faceArea := float32((face.bottom-face.top)*(face.right-face.left)) / imgArea

		if ind < 5 {
			log.Printf("Confidence %.3f top %d bottom %d left %d right %d area %.3f \n", face.confidence, face.top, face.bottom, face.left, face.right, faceArea)

		}

		if face.confidence > float32(*faceDetectionTreshold) && faceArea >= float32(*faceCoveragePercentPtr) {

			numDetectedFaces++
			if *debug {
				gocv.Rectangle(&img, image.Rect(face.left, face.top, face.right, face.bottom), color.RGBA{0, 255, 0, 0}, 2)
			}
		}
	}

	if *debug {

		window.IMShow(img)
		if window.WaitKey(1) >= 0 {
			return
		}
	}
	if numDetectedFaces == 0 {
		log.Println("No face detected in front of computer - sleeping display")
		sleepDisplay()

	}

}

func sleepDisplay() {
	cmd := exec.Command("pmset", "displaysleepnow")
	err := cmd.Run()
	if err != nil {
		log.Printf("Error executing command: %d \n", err)

	}
}

func readImage(deviceID int, img *gocv.Mat) bool {

	for i := 0; i < 10; i++ {

		// open webcam
		webcam, err := gocv.OpenVideoCapture(deviceID)
		webcam.Set(3, 640)
		webcam.Set(4, 480)

		if err != nil {
			log.Println("Cannot open webcam")
			return false
		}

		// We need to sleep a little so the image stabilizes before reading it
		time.Sleep(1 * time.Second)

		ok := webcam.Read(img)
		webcam.Close()

		if ok && !img.Empty() {
			return true
		}

		log.Println("Cannot read image, sleeping for retry...")
		time.Sleep(200 * time.Millisecond)
	}

	log.Println("Cannot read from webcam")
	return false

}

func detectFaces(frame *gocv.Mat, results gocv.Mat) []detectedFace {

	faces := make([]detectedFace, results.Total())

	for i := 0; i < results.Total(); i += 7 {

		faces[i].confidence = results.GetFloatAt(0, i+2)
		faces[i].left = int(results.GetFloatAt(0, i+3) * float32(frame.Cols()))
		faces[i].top = int(results.GetFloatAt(0, i+4) * float32(frame.Rows()))
		faces[i].right = int(results.GetFloatAt(0, i+5) * float32(frame.Cols()))
		faces[i].bottom = int(results.GetFloatAt(0, i+6) * float32(frame.Rows()))

	}

	return faces
}
