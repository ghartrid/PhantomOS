#!/usr/bin/env python3
"""
PhantomOS Face Tracking for Drawing
Uses OpenCV and MediaPipe for face landmark detection

Outputs face position and gestures to stdout in JSON format for the C GUI to read.
Format: {"x": 0.5, "y": 0.5, "gesture": "none", "face_detected": true, "fps": 30.0}

Usage: face_track.py [--mode MODE] [--camera ID] [--preview]
"""

import sys
import os
import json
import argparse
import time
import urllib.request

# Add the virtual environment to path
venv_path = os.path.expanduser("~/.phantomos-venv/lib/python3.12/site-packages")
if os.path.exists(venv_path):
    sys.path.insert(0, venv_path)

try:
    import cv2
    import numpy as np
except ImportError:
    print(json.dumps({"error": "OpenCV not installed. Run: pip install opencv-python"}),
          file=sys.stdout, flush=True)
    sys.exit(1)

# Try to import MediaPipe Tasks API (0.10.x)
HAVE_MEDIAPIPE = False
FaceLandmarker = None
FaceLandmarkerOptions = None
BaseOptions = None
VisionRunningMode = None

try:
    import mediapipe as mp
    from mediapipe.tasks import python as mp_python
    from mediapipe.tasks.python import vision
    FaceLandmarker = vision.FaceLandmarker
    FaceLandmarkerOptions = vision.FaceLandmarkerOptions
    BaseOptions = mp_python.BaseOptions
    VisionRunningMode = vision.RunningMode
    HAVE_MEDIAPIPE = True
except ImportError as e:
    print(json.dumps({"warning": f"MediaPipe tasks not available: {e}"}),
          file=sys.stderr, flush=True)

# Face mesh landmark indices (same indices work with new API)
NOSE_TIP = 4
LEFT_EYE_OUTER = 33
LEFT_EYE_INNER = 133
RIGHT_EYE_OUTER = 362
RIGHT_EYE_INNER = 263
LEFT_EYE_TOP = 159
LEFT_EYE_BOTTOM = 145
RIGHT_EYE_TOP = 386
RIGHT_EYE_BOTTOM = 374
UPPER_LIP = 13
LOWER_LIP = 14
LEFT_MOUTH = 61
RIGHT_MOUTH = 291
LEFT_EYEBROW = 70
RIGHT_EYEBROW = 300
CHIN = 152
FOREHEAD = 10

# Model download URL
MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task"
MODEL_PATH = os.path.expanduser("~/.phantomos/models/face_landmarker.task")


def download_model():
    """Download the face landmarker model if not present"""
    if os.path.exists(MODEL_PATH):
        return True

    model_dir = os.path.dirname(MODEL_PATH)
    os.makedirs(model_dir, exist_ok=True)

    print(json.dumps({"status": "downloading_model"}), file=sys.stdout, flush=True)
    try:
        urllib.request.urlretrieve(MODEL_URL, MODEL_PATH)
        return True
    except Exception as e:
        print(json.dumps({"error": f"Failed to download model: {e}"}),
              file=sys.stdout, flush=True)
        return False


class FaceLandmark:
    """Simple wrapper to match old API landmark access"""
    def __init__(self, x, y, z=0):
        self.x = x
        self.y = y
        self.z = z


class FaceTracker:
    def __init__(self, mode='nose', camera_id=0, show_preview=False):
        self.mode = mode
        self.camera_id = camera_id
        self.show_preview = show_preview

        # Initialize camera
        self.cap = None
        self.landmarker = None

        # Initialize MediaPipe face landmarker
        global HAVE_MEDIAPIPE
        if HAVE_MEDIAPIPE:
            if not download_model():
                HAVE_MEDIAPIPE = False
            else:
                try:
                    options = FaceLandmarkerOptions(
                        base_options=BaseOptions(model_asset_path=MODEL_PATH),
                        running_mode=VisionRunningMode.VIDEO,
                        num_faces=1,
                        min_face_detection_confidence=0.3,
                        min_face_presence_confidence=0.3,
                        min_tracking_confidence=0.3,
                        output_face_blendshapes=False,
                        output_facial_transformation_matrixes=False
                    )
                    self.landmarker = FaceLandmarker.create_from_options(options)
                except Exception as e:
                    print(json.dumps({"warning": f"Could not create landmarker: {e}"}),
                          file=sys.stderr, flush=True)
                    self.landmarker = None

        if not HAVE_MEDIAPIPE or not self.landmarker:
            # Fallback to OpenCV Haar cascade
            cascade_path = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
            self.face_cascade = cv2.CascadeClassifier(cascade_path)

        # Gesture detection state
        self.left_eye_closed_frames = 0
        self.right_eye_closed_frames = 0
        self.mouth_open_frames = 0
        self.last_gesture = "none"
        self.gesture_cooldown = 0

        # Smoothing
        self.smooth_x = 0.5
        self.smooth_y = 0.5
        self.smoothing_factor = 0.3

        # Statistics
        self.frame_count = 0
        self.start_time = time.time()
        self.timestamp_ms = 0

    def start(self):
        """Start the camera"""
        import subprocess
        import glob

        # Check if video devices exist
        video_devices = glob.glob('/dev/video*')
        if not video_devices:
            # Check if uvcvideo module is loaded
            module_loaded = False
            try:
                with open('/proc/modules', 'r') as f:
                    modules = f.read()
                    module_loaded = 'uvcvideo' in modules
            except:
                pass

            if not module_loaded:
                # Try to load the module using pkexec (will show GUI password dialog)
                print(json.dumps({"status": "loading_camera_driver"}), file=sys.stdout, flush=True)
                script_dir = os.path.dirname(os.path.abspath(__file__))
                loader_script = os.path.join(script_dir, "load_webcam.sh")

                if os.path.exists(loader_script):
                    try:
                        result = subprocess.run(['pkexec', 'bash', loader_script],
                                              capture_output=True, timeout=60)
                        time.sleep(1)
                        video_devices = glob.glob('/dev/video*')
                    except subprocess.TimeoutExpired:
                        print(json.dumps({"error": "Timeout waiting for password. Run: sudo modprobe uvcvideo"}),
                              file=sys.stdout, flush=True)
                        return False
                    except Exception as e:
                        print(json.dumps({"error": f"Could not load driver: {e}. Run: sudo modprobe uvcvideo"}),
                              file=sys.stdout, flush=True)
                        return False

            if not video_devices:
                video_devices = glob.glob('/dev/video*')

            if not video_devices:
                print(json.dumps({"error": "No camera found. Run: sudo modprobe uvcvideo"}),
                      file=sys.stdout, flush=True)
                return False

        self.cap = cv2.VideoCapture(self.camera_id)
        if not self.cap.isOpened():
            # Try other camera indices
            for i in range(3):
                self.cap = cv2.VideoCapture(i)
                if self.cap.isOpened():
                    break

        if not self.cap.isOpened():
            print(json.dumps({"error": f"Could not open camera. Available devices: {video_devices}"}),
                  file=sys.stdout, flush=True)
            return False

        # Set resolution
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        self.cap.set(cv2.CAP_PROP_FPS, 30)

        return True

    def stop(self):
        """Stop the camera"""
        if self.cap:
            self.cap.release()
            self.cap = None
        if self.landmarker:
            self.landmarker.close()
            self.landmarker = None
        if self.show_preview:
            cv2.destroyAllWindows()

    def calculate_eye_aspect_ratio(self, landmarks, eye_indices):
        """Calculate eye aspect ratio for blink detection"""
        if not landmarks:
            return 1.0

        top = landmarks[eye_indices[0]]
        bottom = landmarks[eye_indices[1]]
        left = landmarks[eye_indices[2]]
        right = landmarks[eye_indices[3]]

        # Vertical distance
        v_dist = np.sqrt((top.x - bottom.x)**2 + (top.y - bottom.y)**2)
        # Horizontal distance
        h_dist = np.sqrt((left.x - right.x)**2 + (left.y - right.y)**2)

        if h_dist < 0.001:
            return 1.0

        return v_dist / h_dist

    def calculate_mouth_aspect_ratio(self, landmarks):
        """Calculate mouth aspect ratio for open mouth detection"""
        if not landmarks:
            return 0.0

        upper = landmarks[UPPER_LIP]
        lower = landmarks[LOWER_LIP]
        left = landmarks[LEFT_MOUTH]
        right = landmarks[RIGHT_MOUTH]

        # Vertical distance
        v_dist = np.sqrt((upper.x - lower.x)**2 + (upper.y - lower.y)**2)
        # Horizontal distance
        h_dist = np.sqrt((left.x - right.x)**2 + (left.y - right.y)**2)

        if h_dist < 0.001:
            return 0.0

        return v_dist / h_dist

    def detect_gesture(self, landmarks):
        """Detect facial gestures"""
        gesture = "none"

        if not landmarks:
            return gesture

        # Eye aspect ratios
        left_ear = self.calculate_eye_aspect_ratio(
            landmarks,
            [LEFT_EYE_TOP, LEFT_EYE_BOTTOM, LEFT_EYE_INNER, LEFT_EYE_OUTER]
        )
        right_ear = self.calculate_eye_aspect_ratio(
            landmarks,
            [RIGHT_EYE_TOP, RIGHT_EYE_BOTTOM, RIGHT_EYE_INNER, RIGHT_EYE_OUTER]
        )

        # Mouth aspect ratio
        mar = self.calculate_mouth_aspect_ratio(landmarks)

        # Blink detection (EAR threshold ~0.2 for closed eye)
        left_closed = left_ear < 0.15
        right_closed = right_ear < 0.15

        if left_closed:
            self.left_eye_closed_frames += 1
        else:
            self.left_eye_closed_frames = 0

        if right_closed:
            self.right_eye_closed_frames += 1
        else:
            self.right_eye_closed_frames = 0

        # Mouth open detection (MAR threshold ~0.5 for open)
        if mar > 0.4:
            self.mouth_open_frames += 1
        else:
            self.mouth_open_frames = 0

        # Gesture recognition with cooldown
        if self.gesture_cooldown > 0:
            self.gesture_cooldown -= 1
        else:
            # Both eyes blink
            if self.left_eye_closed_frames > 2 and self.right_eye_closed_frames > 2:
                gesture = "blink_both"
                self.gesture_cooldown = 15
            # Left eye wink
            elif self.left_eye_closed_frames > 3 and self.right_eye_closed_frames == 0:
                gesture = "blink_left"
                self.gesture_cooldown = 15
            # Right eye wink
            elif self.right_eye_closed_frames > 3 and self.left_eye_closed_frames == 0:
                gesture = "blink_right"
                self.gesture_cooldown = 15
            # Mouth open
            elif self.mouth_open_frames > 5:
                gesture = "mouth_open"
                self.gesture_cooldown = 20

        return gesture

    def get_tracking_point(self, landmarks, frame_width, frame_height):
        """Get the tracking point based on mode"""
        if not landmarks:
            return None, None

        if self.mode == 'nose':
            point = landmarks[NOSE_TIP]
            return point.x, point.y
        elif self.mode == 'head':
            # Average of forehead and chin
            forehead = landmarks[FOREHEAD]
            chin = landmarks[CHIN]
            x = (forehead.x + chin.x) / 2
            y = (forehead.y + chin.y) / 2
            return x, y
        elif self.mode == 'eyes':
            # Average of eye centers
            left_x = (landmarks[LEFT_EYE_INNER].x + landmarks[LEFT_EYE_OUTER].x) / 2
            left_y = (landmarks[LEFT_EYE_INNER].y + landmarks[LEFT_EYE_OUTER].y) / 2
            right_x = (landmarks[RIGHT_EYE_INNER].x + landmarks[RIGHT_EYE_OUTER].x) / 2
            right_y = (landmarks[RIGHT_EYE_INNER].y + landmarks[RIGHT_EYE_OUTER].y) / 2
            return (left_x + right_x) / 2, (left_y + right_y) / 2
        elif self.mode == 'mouth':
            # Mouth center
            x = (landmarks[LEFT_MOUTH].x + landmarks[RIGHT_MOUTH].x) / 2
            y = (landmarks[UPPER_LIP].y + landmarks[LOWER_LIP].y) / 2
            return x, y

        return 0.5, 0.5

    def process_frame(self):
        """Process a single frame and return tracking data"""
        if not self.cap or not self.cap.isOpened():
            return None

        ret, frame = self.cap.read()
        if not ret:
            return None

        self.frame_count += 1
        self.timestamp_ms += 33  # ~30 FPS
        frame_height, frame_width = frame.shape[:2]

        # Flip horizontally for mirror effect
        frame = cv2.flip(frame, 1)

        # Convert to RGB for MediaPipe
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        result = {
            "x": 0.5,
            "y": 0.5,
            "gesture": "none",
            "face_detected": False,
            "fps": 0.0
        }

        landmarks = None

        if HAVE_MEDIAPIPE and self.landmarker:
            # Process with MediaPipe Tasks API
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
            detection_result = self.landmarker.detect_for_video(mp_image, self.timestamp_ms)

            if detection_result.face_landmarks and len(detection_result.face_landmarks) > 0:
                # Convert to our FaceLandmark wrapper format
                raw_landmarks = detection_result.face_landmarks[0]
                landmarks = [FaceLandmark(lm.x, lm.y, lm.z) for lm in raw_landmarks]

                # Get tracking point
                x, y = self.get_tracking_point(landmarks, frame_width, frame_height)

                if x is not None:
                    # Apply smoothing
                    self.smooth_x = self.smooth_x * self.smoothing_factor + x * (1 - self.smoothing_factor)
                    self.smooth_y = self.smooth_y * self.smoothing_factor + y * (1 - self.smoothing_factor)

                    result["x"] = self.smooth_x
                    result["y"] = self.smooth_y
                    result["face_detected"] = True

                    # Detect gesture
                    gesture = self.detect_gesture(landmarks)
                    result["gesture"] = gesture

                # Draw preview if enabled
                if self.show_preview:
                    # Draw face landmarks
                    for lm in raw_landmarks:
                        px = int(lm.x * frame_width)
                        py = int(lm.y * frame_height)
                        cv2.circle(frame, (px, py), 1, (0, 255, 0), -1)

                    # Draw tracking point
                    px = int(self.smooth_x * frame_width)
                    py = int(self.smooth_y * frame_height)
                    cv2.circle(frame, (px, py), 10, (0, 0, 255), -1)
        else:
            # Fallback to Haar cascade
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = self.face_cascade.detectMultiScale(gray, 1.1, 4)

            if len(faces) > 0:
                x, y, w, h = faces[0]
                center_x = (x + w / 2) / frame_width
                center_y = (y + h / 2) / frame_height

                self.smooth_x = self.smooth_x * self.smoothing_factor + center_x * (1 - self.smoothing_factor)
                self.smooth_y = self.smooth_y * self.smoothing_factor + center_y * (1 - self.smoothing_factor)

                result["x"] = self.smooth_x
                result["y"] = self.smooth_y
                result["face_detected"] = True

                if self.show_preview:
                    cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)

        # Calculate FPS
        elapsed = time.time() - self.start_time
        if elapsed > 0:
            result["fps"] = self.frame_count / elapsed

        # Show preview window
        if self.show_preview:
            cv2.putText(frame, f"Mode: {self.mode}", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.putText(frame, f"FPS: {result['fps']:.1f}", (10, 60),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.putText(frame, f"Gesture: {result['gesture']}", (10, 90),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.putText(frame, f"Face: {'Yes' if result['face_detected'] else 'No'}", (10, 120),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow("PhantomOS Face Tracking", frame)

            # Check for quit key
            if cv2.waitKey(1) & 0xFF == ord('q'):
                return None

        return result

    def run(self):
        """Main tracking loop"""
        if not self.start():
            print(json.dumps({"error": "Could not open camera"}),
                  file=sys.stdout, flush=True)
            return

        print(json.dumps({"status": "started", "mode": self.mode,
                          "mediapipe": HAVE_MEDIAPIPE and self.landmarker is not None}),
              file=sys.stdout, flush=True)

        try:
            while True:
                result = self.process_frame()
                if result is None:
                    break

                # Output JSON to stdout
                print(json.dumps(result), file=sys.stdout, flush=True)

        except KeyboardInterrupt:
            pass
        finally:
            self.stop()
            print(json.dumps({"status": "stopped"}),
                  file=sys.stdout, flush=True)


def main():
    parser = argparse.ArgumentParser(description='Face tracking for PhantomOS drawing')
    parser.add_argument('--mode', type=str, default='nose',
                        choices=['nose', 'head', 'eyes', 'mouth'],
                        help='Tracking mode (default: nose)')
    parser.add_argument('--camera', type=int, default=0,
                        help='Camera device ID (default: 0)')
    parser.add_argument('--preview', action='store_true',
                        help='Show preview window')
    parser.add_argument('--smoothing', type=float, default=0.3,
                        help='Smoothing factor 0.0-1.0 (default: 0.3)')
    args = parser.parse_args()

    tracker = FaceTracker(
        mode=args.mode,
        camera_id=args.camera,
        show_preview=args.preview
    )
    tracker.smoothing_factor = args.smoothing
    tracker.run()


if __name__ == '__main__':
    main()
