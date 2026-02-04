#!/usr/bin/env python3
"""
PhantomOS Voice Recognition Helper
Uses Vosk for offline speech-to-text with arecord for audio capture

Usage: voice_recognize.py [--duration SECONDS] [--model PATH]
"""

import sys
import os
import json
import argparse
import subprocess
import wave
import tempfile

# Add the virtual environment to path
venv_path = os.path.expanduser("~/.phantomos-venv/lib/python3.12/site-packages")
if os.path.exists(venv_path):
    sys.path.insert(0, venv_path)

from vosk import Model, KaldiRecognizer

def record_audio(duration, sample_rate, output_file):
    """Record audio using arecord"""
    try:
        cmd = [
            'arecord',
            '-q',                    # Quiet
            '-f', 'S16_LE',         # Format: signed 16-bit little-endian
            '-r', str(sample_rate), # Sample rate
            '-c', '1',              # Mono
            '-t', 'wav',            # WAV format
            '-d', str(int(duration)), # Duration in seconds
            output_file
        ]
        subprocess.run(cmd, check=True, timeout=duration + 5)
        return True
    except subprocess.TimeoutExpired:
        return True  # Recording completed
    except FileNotFoundError:
        print("Error: arecord not found. Install alsa-utils.", file=sys.stderr)
        return False
    except subprocess.CalledProcessError as e:
        print(f"Error recording: {e}", file=sys.stderr)
        return False

def recognize_audio(model_path, audio_file, sample_rate):
    """Recognize speech from audio file using Vosk"""
    try:
        model = Model(model_path)
    except Exception as e:
        print(f"Error loading model: {e}", file=sys.stderr)
        return None

    recognizer = KaldiRecognizer(model, sample_rate)
    recognizer.SetWords(True)

    results = []

    try:
        wf = wave.open(audio_file, 'rb')
        if wf.getnchannels() != 1 or wf.getsampwidth() != 2:
            print("Audio must be mono 16-bit", file=sys.stderr)
            return None

        while True:
            data = wf.readframes(4000)
            if len(data) == 0:
                break
            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                if result.get('text'):
                    results.append(result['text'])

        # Get final result
        final = json.loads(recognizer.FinalResult())
        if final.get('text'):
            results.append(final['text'])

        wf.close()
    except Exception as e:
        print(f"Error processing audio: {e}", file=sys.stderr)
        return None

    return ' '.join(results)

def main():
    parser = argparse.ArgumentParser(description='Voice recognition for PhantomOS')
    parser.add_argument('--duration', type=float, default=5.0,
                        help='Recording duration in seconds (default: 5)')
    parser.add_argument('--model', type=str,
                        default=os.path.expanduser('~/.phantomos/models/vosk-model-small-en-us-0.15'),
                        help='Path to Vosk model')
    parser.add_argument('--sample-rate', type=int, default=16000,
                        help='Audio sample rate (default: 16000)')
    args = parser.parse_args()

    # Check model exists
    if not os.path.exists(args.model):
        print(f"Error: Model not found at {args.model}", file=sys.stderr)
        print("Download from: https://alphacephei.com/vosk/models", file=sys.stderr)
        sys.exit(1)

    # Create temp file for audio
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
        audio_file = tmp.name

    try:
        print("Listening...", file=sys.stderr)

        # Record audio
        if not record_audio(args.duration, args.sample_rate, audio_file):
            sys.exit(1)

        print("Processing...", file=sys.stderr)

        # Recognize speech
        text = recognize_audio(args.model, audio_file, args.sample_rate)

        if text:
            print(text)
        else:
            print("(no speech detected)", file=sys.stderr)

    finally:
        # Cleanup temp file
        if os.path.exists(audio_file):
            os.unlink(audio_file)

if __name__ == '__main__':
    main()
