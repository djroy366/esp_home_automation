#!/usr/bin/env python3
import speech_recognition as sr
import pyttsx3
import requests
import time

class EmmaAssistant:
    def __init__(self):
        self.engine = self._init_engine()
        self.recognizer = self._init_recognizer()
        self.esp_ip = "192.168.0.201"
        self.esp_port = "8001"
        
    def _init_engine(self):
        engine = pyttsx3.init()
        
        # Voice quality improvements
        engine.setProperty('rate', 160)  # Natural speaking pace
        engine.setProperty('volume', 1.0)
        
        # Try to find the best available voice
        voices = engine.getProperty('voices')
        for voice in voices:
            if 'mbrola' in voice.id.lower() or 'female' in voice.name.lower():
                engine.setProperty('voice', voice.id)
                break
                
        return engine
    
    def _init_recognizer(self):
        r = sr.Recognizer()
        r.energy_threshold = 3000  # Lower for better sensitivity
        r.dynamic_energy_threshold = True
        r.pause_threshold = 0.8  # Shorter pause before considering speech ended
        return r
    
    def speak(self, text):
        """Natural sounding speech with improved pacing"""
        print(f"Emma: {text}")
        self.engine.say(text)
        self.engine.runAndWait()
        time.sleep(0.3)  # Brief pause between phrases
    
    def listen(self):
        """More reliable listening with retry logic"""
        for attempt in range(3):  # Try 3 times
            try:
                with sr.Microphone() as source:
                    print("\nListening... (speak now)")
                    self.recognizer.adjust_for_ambient_noise(source, duration=1)
                    audio = self.recognizer.listen(source, timeout=5, phrase_time_limit=5)
                    text = self.recognizer.recognize_google(audio).lower()
                    print(f"Heard: {text}")
                    return text
            except sr.WaitTimeoutError:
                print("Listening timed out")
            except sr.UnknownValueError:
                print("Didn't catch that")
            except Exception as e:
                print(f"Audio error: {e}")
                
        return ""  # Return empty if all attempts fail
    
    def control_lights(self, state):
        try:
            for i in range(8):
                requests.post(
                    f"http://{self.esp_ip}:{self.esp_port}/relay?num={i}&state={int(state)}",
                    timeout=2
                )
            return True
        except Exception as e:
            print(f"Control error: {e}")
            return False

if __name__ == "__main__":
    emma = EmmaAssistant()
    emma.speak("Hello boss, I'm ready to help")
    
    while True:
        command = emma.listen()
        
        if "hay" in command:
            emma.speak("Yes boss?")
            command = emma.listen()
            
            if "on" in command:
                emma.speak("Turning on all lights now")
                if emma.control_lights(True):
                    emma.speak("Lights are on, boss")
                else:
                    emma.speak("Something went wrong")
                    
            elif "off" in command:
                emma.speak("Turning off all lights")
                if emma.control_lights(False):
                    emma.speak("All lights are off now")
                else:
                    emma.speak("Had trouble with that")
                    
            elif "exit" in command:
                emma.speak("Goodbye boss")
                break
