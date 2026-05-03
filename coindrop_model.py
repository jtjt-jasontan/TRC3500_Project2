import pandas as pd
from sklearn.ensemble import RandomForestClassifier
import joblib

# 1. Load the new 7-feature dataset
df = pd.read_csv('report_data.csv')

# 2. Select all 7 features
feature_cols = [
    'peak_ac', 'rms_ac', 'zero_crossings', 'spectral_centroid', 
    'peak_freq_1', 'peak_freq_2', 'peak_freq_3'
]
X = df[feature_cols]

# 3. Create Labels
y = df['height'].astype(str) + "h_" + df['distance'].astype(str) + "d"

# 4. Train and Save
model = RandomForestClassifier(n_estimators=100, random_state=42)
model.fit(X, y)
joblib.dump(model, 'coin_model_real.pkl')
print("Model created: coin_model_real.pkl")