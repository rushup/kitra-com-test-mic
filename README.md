Example showing how to use microphones of the Kitra520.

Firmware V1E2 is required.

Run:

```
cmake .
make
./kitra_com_exe
ffmpeg -f s16le -ar 16000 -i pcm_audio_1.raw out.wav
```

Now you can play out.wav and hear the recording.