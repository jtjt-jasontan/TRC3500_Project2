import argparse
from datetime import datetime
from pathlib import Path

import serial
import numpy as np
import matplotlib

# Use a non-interactive backend by default
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SAMPLE_RATE_HZ = 20_000 
ADC_MAX        = 4095   
VREF           = 3.3    

import numpy as np
from scipy.signal import find_peaks

def get_features(samples, fs):
    # 1. Remove DC Baseline (from our previous fix)
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
    
    # Spectral Centroid (Still great, keep this!)
    spectral_centroid = np.sum(freqs * fft_vals) / (np.sum(fft_vals) + 1e-6)
    
    # --- NEW: Top 3 Peak Frequencies ---
    # Find all peaks in the FFT
    peaks, properties = find_peaks(fft_vals, distance=10) # distance prevents picking neighboring bins
    
    # Get the amplitudes of the peaks
    peak_amps = fft_vals[peaks]
    
    # Sort peaks by amplitude (highest to lowest) and get the top 3 indices
    top_indices = np.argsort(peak_amps)[-3:][::-1]
    
    # Get the actual frequencies of those top 3 peaks
    top_3_freqs = freqs[peaks[top_indices]]
    
    # Pad with zeros if it somehow found less than 3 peaks
    while len(top_3_freqs) < 3:
        top_3_freqs = np.append(top_3_freqs, 0)
        
    # Sort the top 3 frequencies from lowest pitch to highest pitch 
    # (So the AI always sees Low, Mid, High order)
    top_3_freqs = np.sort(top_3_freqs)
    
    return [
        peak_ac, 
        rms_ac, 
        zero_crossings, 
        spectral_centroid, 
        top_3_freqs[0], # Peak Freq 1
        top_3_freqs[1], # Peak Freq 2
        top_3_freqs[2]  # Peak Freq 3
    ]

def plot_capture(samples, out_path, title_suffix=""):
    samples = np.asarray(samples, dtype=float)
    n       = len(samples)
    fs      = SAMPLE_RATE_HZ
    t_ms    = np.arange(n) / fs * 1000.0

    peak_adc_val = np.max(samples)
    peak_adc_idx = np.argmax(samples)
    peak_adc_t   = t_ms[peak_adc_idx]

    baseline = np.mean(samples)
    ac       = samples - baseline

    window   = np.hanning(n)
    windowed = ac * window
    fft_vals = np.fft.rfft(windowed)
    freqs    = np.fft.rfftfreq(n, d=1.0 / fs)
    mag      = np.abs(fft_vals) * 2.0 / np.sum(window)

    fig, (ax_time, ax_freq) = plt.subplots(2, 1, figsize=(10, 7))

    ax_time.plot(t_ms, samples, color="#1f77b4", linewidth=0.9)
    ax_time.plot(peak_adc_t, peak_adc_val, "ro", markersize=4, label=f"Peak: {peak_adc_val:.0f}")
    ax_time.annotate(f"Max: {peak_adc_val:.0f}", 
                     xy=(peak_adc_t, peak_adc_val), 
                     xytext=(10, 5), textcoords="offset points",
                     color="red", weight="bold", fontsize=9)

    ax_time.axhline(baseline, color="gray", linestyle="--", linewidth=0.8,
                    label=f"baseline = {baseline:.1f}")
    ax_time.set_xlabel("Time (ms)")
    ax_time.set_ylabel("ADC value (counts)")
    ax_time.set_title(f"Coin-drop waveform{title_suffix}")
    ax_time.grid(True, alpha=0.3)
    ax_time.legend(loc="upper right", fontsize=9)

    def counts_to_v(x):   return x * (VREF / ADC_MAX)
    def v_to_counts(x):   return x / (VREF / ADC_MAX)
    ax_time_v = ax_time.secondary_yaxis("right", functions=(counts_to_v, v_to_counts))
    ax_time_v.set_ylabel("Voltage (V)")

    ax_freq.plot(freqs, mag, color="#d62728", linewidth=0.9)
    ax_freq.set_xlabel("Frequency (Hz)")
    ax_freq.set_ylabel("Magnitude (ADC counts)")
    ax_freq.set_xlim(0, fs / 2)
    ax_freq.grid(True, alpha=0.3)

    skip     = max(1, int(20 * n / fs))
    peak_idx = np.argmax(mag[skip:]) + skip
    peak_f   = freqs[peak_idx]
    peak_m   = mag[peak_idx]
    ax_freq.plot(peak_f, peak_m, "o", color="black", markersize=5)
    ax_freq.annotate(f"peak: {peak_f:.0f} Hz", xy=(peak_f, peak_m), xytext=(10, -5), 
                     textcoords="offset points", fontsize=9)

    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    return fig, peak_f, peak_adc_val

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", default="report_captures")
    parser.add_argument("--h_label", type=int, help="Height label for training")
    parser.add_argument("--d_label", type=int, help="Distance label for training")
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    if args.show:
        matplotlib.use("TkAgg", force=True)
        plt.ion()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"Listening on {args.port}. Mode: {'TRAINING' if args.h_label else 'MONITORING'}")

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
                    # 1. Create the Timestamp and Filename prefix
                    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                    
                    # Use labels if available, otherwise use 'NA'
                    h = args.h_label if args.h_label is not None else "NA"
                    d = args.d_label if args.d_label is not None else "NA"
                    
                    # Target filename format: COINDROP_<distance>_<height>_<date/time>
                    file_prefix = f"COINDROP_{d}_{h}_{ts}"
                    
                    adc_values = [v for _, v in buffer]
                    
                    # 2. Save Raw CSV
                    csv_path = out_dir / f"{file_prefix}.csv"
                    with open(csv_path, "w") as f:
                        f.write("sample_index,adc_value\n")
                        for idx, val in buffer: f.write(f"{idx},{val}\n")

                    # 3. Add to Master Training Dataset
                    if args.h_label is not None and args.d_label is not None:
                        master_csv = "report_data.csv"
                        exists = Path(master_csv).exists()
                        feats = get_features(adc_values, SAMPLE_RATE_HZ)
                        with open(master_csv, "a") as f:
                            if not exists:
                                f.write("peak_ac,rms_ac,zero_crossings,spectral_centroid,peak_freq_1,peak_freq_2,peak_freq_3,height,distance\n")
                            f.write(f"{feats[0]},{feats[1]},{feats[2]},{feats[3]},{feats[4]},{feats[5]},{feats[6]},{args.h_label},{args.d_label}\n")
                        print(f"    [DATASET] Saved features to report_data.csv")

                    # 4. Plotting
                    try:
                        png_path = out_dir / f"{file_prefix}.png"
                        fig, peak_f, peak_adc = plot_capture(adc_values, png_path, f" ({file_prefix})")
                        print(f"    -> Saved: {png_path.name}")
                        print(f"    -> Peak: {peak_adc} | Freq: {peak_f:.0f}Hz")
                        if args.show:
                            plt.show(block=False); plt.pause(0.1)
                        else:
                            plt.close(fig)
                    except Exception as e:
                        print(f"Plotting error: {e}")
                continue

            if in_csv and "," in line:
                try:
                    p = line.split(",")
                    buffer.append((int(p[0]), int(p[1])))
                except: pass

    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()