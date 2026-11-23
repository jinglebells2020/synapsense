DET := ../lib/detector

replay: replay.cpp $(DET)/features.cpp $(DET)/fft.cpp $(DET)/detector.cpp $(DET)/*.h
	c++ -std=c++17 -O2 -Wall -I$(DET) -o $@ replay.cpp $(DET)/features.cpp $(DET)/fft.cpp $(DET)/detector.cpp

# Real-data check: everyday handling must not alert, the 48 s shake must alert once.
check: replay
	@./replay ../recordings/2026-11-23_wear_then_30s_shake.csv --quiet | tee /dev/stderr | grep -c ">>> ALERT" | grep -qx 1 && echo "recording check: PASS (1 alert)" || (echo "recording check: FAIL"; exit 1)
