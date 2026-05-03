import argparse
import serial
import joblib
import numpy as np
from pathlib import Path
from scipy.signal import find_peaks

# Constants matching your hardware setup
SAMPLE_RATE_HZ = 20_000 

def get_features(samples, fs):
    """Extracts baseline-immune 'DNA' of the coin drop."""
    # 1. Remove DC Baseline
    samples = np.asarray(samples, dtype=float)
    ac_samples = samples - np.mean(samples)
    
    # Time Domain
    peak_ac = np.max(np.abs(ac_samples))
    rms_ac = np.sqrt(np.mean(ac_samples**2))
    zero_crossings = np.sum(np.diff(np.sign(ac_samples)) != 0)
    
    # Frequency Domain
    windowed = ac_samples * np.hanning(len(ac_samples))
    fft_vals = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(len(samples), 1/fs)
    
    # Spectral Centroid
    spectral_centroid = np.sum(freqs * fft_vals) / (np.sum(fft_vals) + 1e-6)
    
    # Top 3 Peak Frequencies
    peaks, _ = find_peaks(fft_vals, distance=10)
    peak_amps = fft_vals[peaks]
    
    top_indices = np.argsort(peak_amps)[-3:][::-1]
    top_3_freqs = freqs[peaks[top_indices]]
    
    # Pad with zeros if less than 3 peaks found
    while len(top_3_freqs) < 3:
        top_3_freqs = np.append(top_3_freqs, 0)
        
    top_3_freqs = np.sort(top_3_freqs)
    
    return [
        peak_ac, 
        rms_ac, 
        zero_crossings, 
        spectral_centroid, 
        top_3_freqs[0], 
        top_3_freqs[1], 
        top_3_freqs[2]
    ]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--model", default="coin_model_real.pkl", help="Path to trained model")
    args = parser.parse_args()

    # 1. Load the Trained AI Model
    print(f"Loading AI model: {args.model}...")
    try:
        clf = joblib.load(args.model)
        
        # Verify the model expects 7 features
        expected_features = clf.n_features_in_
        if expected_features != 7:
            print(f"\n[WARNING] Your model expects {expected_features} features, but this script extracts 7.")
            print("Did you forget to retrain the model with the newest training script?")
            return
            
        print("Model loaded successfully! Waiting for coin drops...")
    except Exception as e:
        print(f"Error loading model: {e}")
        print("Make sure you have run your training script first.")
        return

    # 2. Open Serial Connection
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as e:
        print(f"Could not open port {args.port}: {e}")
        return

    in_csv = False
    buffer = []

    try:
        while True:
            raw = ser.readline()
            if not raw: continue
            line = raw.decode("ascii", errors="ignore").strip()

            if line == "BEGIN_CSV":
                in_csv = True
                buffer = []
                continue

            if line == "END_CSV":
                in_csv = False
                if buffer:
                    # Extract raw ADC values from the buffer
                    adc_values = [v for _, v in buffer]
                    
                    # 3. Extract the 7 robust features
                    feats = get_features(adc_values, SAMPLE_RATE_HZ)
                    
                    # 4. Perform AI Prediction
                    prediction = clf.predict([feats])[0]
                    
                    # Split prediction into height and distance for cleaner printing (assuming format "30h_10d")
                    try:
                        h, d = prediction.split('_')
                        h = h.replace('h', ' cm')
                        d = d.replace('d', ' cm')
                    except:
                        h, d = prediction, prediction # Fallback if label format is different
                    
                    # 5. Output Result
                    print("\n" + "="*35)
                    print(f"  COIN DROP DETECTED!")
                    print(f"  Height:   {h}")
                    print(f"  Distance: {d}")
                    print("="*35 + "\n")
                
                continue

            # Accumulate data while between BEGIN and END tags
            if in_csv and "," in line:
                try:
                    p = line.split(",")
                    buffer.append((int(p[0]), int(p[1])))
                except: pass

    except KeyboardInterrupt:
        print("\nStopping AI Logger.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()