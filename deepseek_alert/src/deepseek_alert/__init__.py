#!/usr/bin/env python3
import threading
import time
from datetime import datetime, timedelta
from tkinter import Tk, Label, Button
from zoneinfo import ZoneInfo
import holidays


class DeepSeekMonitorApp:

    def __init__(self):
        self.root = Tk()
        self.root.title("DeepSeek Primetime Monitor")
        self.root.geometry("800x550")

        # Use Courier to keep the tabular data perfectly aligned
        self.label = Label(
            self.root,
            text="Initializing...",
            font=("Courier", 11, "bold"),
            justify="left",
            padx=20,
            pady=20,
        )
        self.label.pack(expand=True, fill="both")

        self.ack_button = Button(
            self.root,
            text="Acknowledge",
            font=("Helvetica", 12, "bold"),
            command=self.acknowledge_and_minimize,
            bg="#ffffff",
            fg="#000000",
            relief="raised",
            bd=4,
            padx=10,
            pady=5,
        )
        self.ack_button_visible = False

        self.peak_windows = [(1, 4), (6, 10)]
        self.current_state = None

        self.force_to_foreground()

        self.monitor_thread = threading.Thread(
            target=self.timing_loop, daemon=True
        )
        self.monitor_thread.start()

    def force_to_foreground(self):
        """Brings the Tkinter window to the front and pulls it out of minimization."""
        self.root.deiconify()
        self.root.lift()
        self.root.attributes("-topmost", True)
        self.root.after(1000, lambda: self.root.attributes("-topmost", False))

    def acknowledge_and_minimize(self):
        self.root.attributes("-fullscreen", False)
        self.root.iconify()

    def trigger_chime(self):
        try:
            self.root.bell()
        except Exception:
            pass

    def is_off_peak_day(self, dt):
        """Returns True if the given UTC datetime falls on a Beijing weekend or holiday."""
        beijing_dt = dt.astimezone(ZoneInfo("Asia/Shanghai"))
        beijing_date = beijing_dt.date()
        cn_holidays = holidays.China(years=beijing_date.year)
        return beijing_dt.weekday() in (5, 6) or beijing_date in cn_holidays

    def get_next_non_peak_windows(self, utc_now, count=3):
        """Scans forward minute-by-minute to find the next N distinct non-peak windows."""
        windows = []
        current_time = utc_now.replace(second=0, microsecond=0)
        
        # Look ahead up to 5 days to safely step over weekends/holidays
        max_look_ahead = current_time + timedelta(days=5)

        while current_time < max_look_ahead and len(windows) < count:
            # Check if current minute is off-peak
            is_off_peak = False
            if self.is_off_peak_day(current_time):
                is_off_peak = True
            else:
                # Weekday rule: check if outside peak windows
                hour = current_time.hour
                in_peak = any(start <= hour < end for start, end in self.peak_windows)
                is_off_peak = not in_peak

            if is_off_peak:
                start_bound = current_time
                # Trace forward to find where this specific off-peak block finishes
                end_bound = start_bound
                while end_bound < max_look_ahead:
                    next_minute = end_bound + timedelta(minutes=1)
                    # Evaluate if next minute changes back to peak
                    next_is_off_peak = False
                    if self.is_off_peak_day(next_minute):
                        next_is_off_peak = True
                    else:
                        next_hour = next_minute.hour
                        next_in_peak = any(start <= next_hour < end for start, end in self.peak_windows)
                        next_is_off_peak = not next_in_peak
                    
                    if not next_is_off_peak:
                        end_bound = next_minute
                        break
                    end_bound = next_minute
                
                # Append found window block and fast-forward loop index past it
                windows.append((start_bound, end_bound))
                current_time = end_bound
            else:
                current_time += timedelta(minutes=1)

        return windows

    def format_countdown(self, delta):
        """Converts a timedelta into an hh:mm countdown string layout."""
        total_seconds = int(delta.total_seconds())
        if total_seconds <= 0:
            return "Active Now"
        hours, remainder = divmod(total_seconds, 3600)
        minutes, _ = divmod(remainder, 60)
        return f"{hours}h:{minutes:02d}m"

    def evaluate_status(self):
        local_now = datetime.now().astimezone()
        utc_now = local_now.astimezone(ZoneInfo("UTC"))
        beijing_now = local_now.astimezone(ZoneInfo("Asia/Shanghai"))

        time_display = (
            f"Local Time:   {local_now.strftime('%Y-%m-%d %H:%M:%S %Z')}\n"
            f"UTC Time:     {utc_now.strftime('%Y-%m-%d %H:%M:%S UTC')}\n"
            f"Beijing Time: {beijing_now.strftime('%Y-%m-%d %H:%M:%S %Z')}\n"
            f"{'-'*76}\n"
        )

        # 1. Determine current pricing state color logic
        if self.is_off_peak_day(utc_now):
            color = "green"
        else:
            in_peak = any(start <= utc_now.hour < end for start, end in self.peak_windows)
            if in_peak:
                color = "red"
            else:
                # Find next peak start today to handle yellow transition criteria
                next_peak_start = None
                for start_hour, _ in self.peak_windows:
                    candidate = utc_now.replace(hour=start_hour, minute=0, second=0, microsecond=0)
                    if candidate > utc_now:
                        next_peak_start = candidate
                        break
                
                if next_peak_start and (next_peak_start - utc_now).total_seconds() / 60.0 <= 15:
                    color = "yellow"
                else:
                    color = "green"

        status_headers = {
            "red": "CRITICAL: DeepSeek Peak Pricing Window Active!",
            "yellow": "WARNING: Peak Window Approaching Soon!",
            "green": "SAFE: DeepSeek Operations are Off-Peak."
        }

        # 2. Fetch the next 3 distinct non-peak windows
        np_windows = self.get_next_non_peak_windows(utc_now, count=3)

        # 3. Construct the comprehensive timetable layout
        table_output = "UPCOMING SCHEDULE: NEXT 3 DISTINCT NON-PEAK WINDOWS\n"
        table_output += f"{'='*76}\n"
        
        for idx, (start_dt, end_dt) in enumerate(np_windows, 1):
            # Calculate zone conversions for start parameters
            s_local = start_dt.astimezone(local_now.tzinfo)
            s_beijing = start_dt.astimezone(beijing_now.tzinfo)
            
            # Calculate zone conversions for end parameters
            e_local = end_dt.astimezone(local_now.tzinfo)
            e_beijing = end_dt.astimezone(beijing_now.tzinfo)

            # Determine temporal countdown metrics from current moment
            countdown_start = self.format_countdown(start_dt - utc_now)
            countdown_end = self.format_countdown(end_dt - utc_now)

            window_label = f"WINDOW #{idx}"
            if idx == 1 and color in ("green", "yellow") and (start_dt <= utc_now < end_dt):
                window_label += " (CURRENTLY ACTIVE)"

            table_output += f"{window_label}\n"
            table_output += f"{'-'*76}\n"
            table_output += f" Zone    | Start Time                     | End Time\n"
            table_output += f"{'-'*76}\n"
            table_output += f" Local   | {s_local.strftime('%Y-%m-%d %H:%M %Z'):<26} | {e_local.strftime('%Y-%m-%d %H:%M %Z')}\n"
            table_output += f" UTC     | {start_dt.strftime('%Y-%m-%d %H:%M UTC'):<26} | {end_dt.strftime('%Y-%m-%d %H:%M UTC')}\n"
            table_output += f" Beijing | {s_beijing.strftime('%Y-%m-%d %H:%M %Z'):<26} | {e_beijing.strftime('%Y-%m-%d %H:%M %Z')}\n"
            table_output += f"{'-'*76}\n"
            table_output += f" Status  | Opens: {countdown_start:<23} | Closes: {countdown_end}\n"
            table_output += f"{'='*76}\n\n"

        final_text = f"{time_display}{status_headers[color]}\n\n{table_output}"
        return color, final_text

    def update_ui(self, color, display_text):
        self.label.config(text=display_text, bg=color)

        if color in ("yellow", "red"):
            self.root.attributes("-fullscreen", True)
            if not self.ack_button_visible:
                self.ack_button.pack(side="bottom", fill="x", padx=20, pady=20)
                self.ack_button_visible = True
        else:
            self.root.attributes("-fullscreen", False)
            if self.ack_button_visible:
                self.ack_button.pack_forget()
                self.ack_button_visible = False

        if color != self.current_state:
            self.current_state = color
            self.force_to_foreground()
            if color in ("yellow", "red"):
                self.trigger_chime()

    def timing_loop(self):
        color, text = self.evaluate_status()
        self.root.after(0, self.update_ui, color, text)

        while True:
            now = datetime.now()
            seconds_to_wait = 60 - now.second - (now.microsecond / 1000000.0)
            time.sleep(seconds_to_wait)

            color, text = self.evaluate_status()
            self.root.after(0, self.update_ui, color, text)

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    time.sleep(10)
    app = DeepSeekMonitorApp()
    app.run()
