import os

def create_svg():
    svg = []
    svg.append('<svg viewBox="0 0 1200 950" xmlns="http://www.w3.org/2000/svg">')
    svg.append('<defs>')
    svg.append('<style>')
    svg.append("text { font-family: 'Segoe UI', Arial, sans-serif; user-select: none; }")
    svg.append(".wire-vcc { stroke: #e74c3c; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-gnd { stroke: #2c3e50; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-i2c { stroke: #3498db; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-i2s { stroke: #9b59b6; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-servo { stroke: #f39c12; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-btn { stroke: #1abc9c; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; opacity: 0.8; }")
    svg.append(".wire-vcc:hover, .wire-gnd:hover, .wire-i2c:hover, .wire-i2s:hover, .wire-servo:hover, .wire-btn:hover { stroke-width: 6; opacity: 1; cursor: pointer; }")
    svg.append(".module { filter: drop-shadow(2px 2px 4px rgba(0,0,0,0.2)); }")
    svg.append(".pin { fill: #f1c40f; }")
    svg.append(".pin-label { font-size: 11px; fill: #ffffff; }")
    svg.append(".pin-label-dark { font-size: 11px; fill: #333333; }")
    svg.append('</style>')
    svg.append('</defs>')
    
    # Background
    svg.append('<rect width="100%" height="100%" fill="#fafafa"/>')
    svg.append('<text x="600" y="50" font-size="28" font-weight="bold" text-anchor="middle" fill="#333">Xiaozhi Dog - Clean Hardware Wiring Diagram</text>')
    
    pins_data = {}
    
    def add_pin(id, x, y, label, is_left=True, color="#f1c40f", is_dark_text=False):
        svg.append(f'<circle cx="{x}" cy="{y}" r="4" class="pin" fill="{color}"/>')
        align = "end" if is_left else "start"
        offset = -8 if is_left else 8
        text_class = "pin-label-dark" if is_dark_text else "pin-label"
        if label:
            svg.append(f'<text x="{x + offset}" y="{y + 4}" class="{text_class}" text-anchor="{align}">{label}</text>')
        pins_data[id] = (x, y)

    # ---------- ESP32 ----------
    esp_x, esp_y = 500, 150
    svg.append(f'<g class="module"><rect x="{esp_x}" y="{esp_y}" width="200" height="600" fill="#222222" rx="8"/>')
    svg.append(f'<rect x="{esp_x+50}" y="{esp_y+50}" width="100" height="120" fill="#444" rx="4"/>')
    svg.append(f'<text x="{esp_x+100}" y="{esp_y+110}" fill="#fff" font-size="16" text-anchor="middle" font-weight="bold">ESP32-S3</text></g>')

    left_labels = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46", "9", "10", "11", "12", "13", "5V", "GND"]
    right_labels = ["GND", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37", "36", "35", "0", "45", "48", "47", "21", "20", "19", "14"]
    
    for i, label in enumerate(left_labels):
        py = esp_y + 40 + i * 25
        add_pin(f"ESP_L_{label}_{i}", esp_x + 10, py, label, is_left=False)
        if label not in pins_data or label == "GND": 
            pins_data[f"ESP_{label}"] = (esp_x + 10, py)

    for i, label in enumerate(right_labels):
        py = esp_y + 40 + i * 25
        add_pin(f"ESP_R_{label}_{i}", esp_x + 190, py, label, is_left=True)
        if label not in pins_data or label == "GND":
            pins_data[f"ESP_{label}"] = (esp_x + 190, py) 

    # Fix ESP specific pin aliases
    esp_3v3_1 = "ESP_L_3V3_0"
    esp_3v3_2 = "ESP_L_3V3_1"
    esp_gnd_l = "ESP_L_GND_20"
    esp_gnd_r1 = "ESP_R_GND_0"
    
    # ---------- INMP441 ----------
    # Put pins on the RIGHT side to avoid messy crossovers
    mic_x, mic_y = 150, 200
    svg.append(f'<g class="module"><circle cx="{mic_x+50}" cy="{mic_y+50}" r="50" fill="#1f3a93"/>')
    svg.append(f'<circle cx="{mic_x+50}" cy="{mic_y+50}" r="20" fill="#e4e9ed"/>')
    svg.append(f'<text x="{mic_x+50}" y="{mic_y+15}" fill="#fff" font-size="14" text-anchor="middle">INMP441</text></g>')
    
    # All pins on right edge for clean routing to ESP
    add_pin("MIC_L/R", mic_x + 95, mic_y + 25, "L/R", is_left=True)
    add_pin("MIC_WS", mic_x + 95, mic_y + 40, "WS", is_left=True)
    add_pin("MIC_SCK", mic_x + 95, mic_y + 55, "SCK", is_left=True)
    add_pin("MIC_VDD", mic_x + 95, mic_y + 70, "VDD", is_left=True)
    add_pin("MIC_GND", mic_x + 95, mic_y + 85, "GND", is_left=True)
    add_pin("MIC_SD", mic_x + 95, mic_y + 100, "SD", is_left=True)

    # ---------- MAX98357A ----------
    spk_x, spk_y = 150, 450
    svg.append(f'<g class="module"><rect x="{spk_x}" y="{spk_y}" width="90" height="120" fill="#8e44ad" rx="5"/>')
    svg.append(f'<text x="{spk_x+45}" y="{spk_y+20}" fill="#fff" font-size="14" text-anchor="middle">MAX98357A</text></g>')
    
    add_pin("SPK_VIN", spk_x + 80, spk_y + 40, "VIN", is_left=True)
    add_pin("SPK_GND", spk_x + 80, spk_y + 55, "GND", is_left=True)
    add_pin("SPK_DIN", spk_x + 80, spk_y + 70, "DIN", is_left=True)
    add_pin("SPK_BCLK", spk_x + 80, spk_y + 85, "BCLK", is_left=True)
    add_pin("SPK_LRC", spk_x + 80, spk_y + 100, "LRC", is_left=True)

    # ---------- OLED ----------
    oled_x, oled_y = 800, 150
    svg.append(f'<g class="module"><rect x="{oled_x}" y="{oled_y}" width="140" height="80" fill="#2980b9" rx="3"/>')
    svg.append(f'<rect x="{oled_x+30}" y="{oled_y+10}" width="100" height="60" fill="#111" />')
    svg.append(f'<text x="{oled_x+80}" y="{oled_y+45}" fill="#0f0" font-size="16" text-anchor="middle">^.^</text></g>')
    
    add_pin("OLED_GND", oled_x + 10, oled_y + 20, "GND", is_left=False)
    add_pin("OLED_VCC", oled_x + 10, oled_y + 35, "VCC", is_left=False)
    add_pin("OLED_SCL", oled_x + 10, oled_y + 50, "SCL", is_left=False)
    add_pin("OLED_SDA", oled_x + 10, oled_y + 65, "SDA", is_left=False)

    # ---------- Buttons ----------
    btn_x = 800
    def draw_btn(id, y, label):
        svg.append(f'<g class="module"><rect x="{btn_x}" y="{y}" width="80" height="30" fill="#95a5a6" rx="3"/>')
        svg.append(f'<circle cx="{btn_x+15}" cy="{y+15}" r="6" fill="#7f8c8d"/>')
        svg.append(f'<text x="{btn_x+30}" y="{y+19}" fill="#333" font-size="12">{label}</text></g>')
        add_pin(f"{id}_PIN", btn_x - 5, y + 15, "", is_left=False)

    draw_btn("BTN_T", 280, "Touch(47)")
    draw_btn("BTN_VU", 330, "Vol+(40)")
    draw_btn("BTN_VD", 380, "Vol-(39)")

    # ---------- Servos ----------
    # Stack them vertically for very clean routing
    sv_x = 850
    def draw_servo(id, y, label):
        svg.append(f'<g class="module"><rect x="{sv_x}" y="{y}" width="80" height="40" fill="#34495e" rx="4"/>')
        svg.append(f'<circle cx="{sv_x+65}" cy="{y+20}" r="10" fill="#bdc3c7"/>')
        svg.append(f'<text x="{sv_x+30}" y="{y+15}" fill="#fff" font-size="10" text-anchor="middle">{label}</text></g>')
        add_pin(f"{id}_SIG", sv_x - 5, y + 10, "SIG", is_left=False)
        add_pin(f"{id}_VCC", sv_x - 5, y + 20, "VCC", is_left=False)
        add_pin(f"{id}_GND", sv_x - 5, y + 30, "GND", is_left=False)

    draw_servo("SV_LF", 500, "Left Front(17)")
    draw_servo("SV_RF", 560, "Right Front(13)")
    draw_servo("SV_LB", 620, "Left Back(18)")
    draw_servo("SV_RB", 680, "Right Back(14)")

    # ---------- External Power ----------
    pwr_x, pwr_y = 850, 780
    svg.append(f'<g class="module"><rect x="{pwr_x}" y="{pwr_y}" width="120" height="60" fill="#f39c12" rx="5"/>')
    svg.append(f'<text x="{pwr_x+60}" y="{pwr_y+25}" fill="#000" font-size="14" text-anchor="middle" font-weight="bold">EXT 5V 2A+</text></g>')
    add_pin("PWR_5V", pwr_x - 5, pwr_y + 40, "5V+", is_left=False, is_dark_text=True)
    add_pin("PWR_GND", pwr_x - 5, pwr_y + 55, "GND", is_left=False, is_dark_text=True)

    # ---------- Clean Wire Routing ----------
    wires = []
    
    # Orthogonal path helper (x1,y1 to x2,y2 with a horizontal break at split_x)
    def route_s(p1_id, p2_id, cls, split_x=None):
        if p1_id not in pins_data or p2_id not in pins_data: return
        x1, y1 = pins_data[p1_id]
        x2, y2 = pins_data[p2_id]
        
        # Clean S-curve (orthogonal-like using Bezier)
        cx = split_x if split_x else x1 + (x2 - x1) * 0.5
        wires.append(f'<path d="M {x1} {y1} C {cx} {y1}, {cx} {y2}, {x2} {y2}" class="{cls}" />')
        
    def route_u(p1_id, p2_id, cls, drop_y):
        # Route down to a specific Y, then across, then up
        if p1_id not in pins_data or p2_id not in pins_data: return
        x1, y1 = pins_data[p1_id]
        x2, y2 = pins_data[p2_id]
        wires.append(f'<path d="M {x1} {y1} C {x1-50} {y1}, {x1-50} {drop_y}, {x1} {drop_y} L {x2} {drop_y} C {x2+50} {drop_y}, {x2+50} {y2}, {x2} {y2}" class="{cls}" />')

    # Power Wiring (VCC)
    route_s("MIC_VDD", "ESP_L_3V3_0", "wire-vcc", 380)
    route_s("SPK_VIN", "ESP_L_3V3_1", "wire-vcc", 380)
    route_s("OLED_VCC", "ESP_R_GND_0", "wire-vcc") # Wait, ESP_R_3V3_0 doesn't exist, we must route OLED_VCC to left side or a 3v3 if available.
    
    # OLED VCC -> Left side 3v3. We can just draw a line that goes OVER the ESP cleanly
    x1, y1 = pins_data["OLED_VCC"]
    x2, y2 = pins_data["ESP_L_3V3_0"]
    wires.append(f'<path d="M {x1} {y1} C 750 {y1}, 600 {y1-60}, {x2} {y2}" class="wire-vcc" />')

    # GND Wiring
    route_s("MIC_L/R", "MIC_GND", "wire-gnd", mic_x + 130) # bridge L/R to GND locally
    route_s("MIC_GND", "ESP_L_GND_20", "wire-gnd", 350)
    route_s("SPK_GND", "ESP_L_GND_20", "wire-gnd", 340)
    route_s("OLED_GND", "ESP_R_GND_0", "wire-gnd", 750)
    
    # Data Wires - MIC (I2S)
    route_s("MIC_WS", "ESP_L_4_3", "wire-i2s", 370)
    route_s("MIC_SCK", "ESP_L_5_4", "wire-i2s", 360)
    route_s("MIC_SD", "ESP_L_6_5", "wire-i2s", 350)

    # Data Wires - SPK (I2S)
    route_s("SPK_DIN", "ESP_L_7_6", "wire-i2s", 330)
    route_s("SPK_BCLK", "ESP_L_15_7", "wire-i2s", 320)
    route_s("SPK_LRC", "ESP_L_16_8", "wire-i2s", 310)

    # Data Wires - OLED (I2C)
    route_s("OLED_SCL", "ESP_R_42_5", "wire-i2c", 760)
    route_s("OLED_SDA", "ESP_R_41_6", "wire-i2c", 770)

    # Buttons
    route_s("BTN_T_PIN", "ESP_R_47_16", "wire-btn", 750)
    route_s("BTN_VU_PIN", "ESP_R_40_7", "wire-btn", 750)
    route_s("BTN_VD_PIN", "ESP_R_39_8", "wire-btn", 750)
    
    # Button GNDs (bridge them and route one to ESP)
    # Buttons don't have explicit GND pins drawn now, they just imply grounded when pressed.
    # To keep it ultra clean, I'll draw a small GND symbol for buttons, or just leave it off since it's documented.
    
    # Servos Wires
    # VCC/GND bus down the right side
    bus_x_vcc = 830
    bus_x_gnd = 840
    
    for sv in ["SV_LF", "SV_RF", "SV_LB", "SV_RB"]:
        # VCC
        sx, sy = pins_data[f"{sv}_VCC"]
        px, py = pins_data["PWR_5V"]
        wires.append(f'<path d="M {sx} {sy} C {bus_x_vcc} {sy}, {bus_x_vcc} {py}, {px} {py}" class="wire-vcc" />')
        # GND
        gx, gy = pins_data[f"{sv}_GND"]
        pgx, pgy = pins_data["PWR_GND"]
        wires.append(f'<path d="M {gx} {gy} C {bus_x_gnd} {gy}, {bus_x_gnd} {pgy}, {pgx} {pgy}" class="wire-gnd" />')

    # Servo Signals
    route_s("SV_LF_SIG", "ESP_L_17_9", "wire-servo")
    route_s("SV_RF_SIG", "ESP_L_13_18", "wire-servo")
    route_s("SV_LB_SIG", "ESP_L_18_10", "wire-servo")
    route_s("SV_RB_SIG", "ESP_R_14_20", "wire-servo", 750)
    
    # For LF, RF, LB that go to the left side of ESP32 from the right side... 
    # Since they are on the right (x=850), and ESP left is x=510, route_s will naturally pull them through the ESP box.
    # To avoid crossing the ESP box, let's route them DOWN and under the ESP32!
    
    wires.pop() # remove RB
    wires.pop() # remove LB
    wires.pop() # remove RF
    wires.pop() # remove LF

    def route_under_esp(p1_id, p2_id, cls, drop_y):
        x1, y1 = pins_data[p1_id]
        x2, y2 = pins_data[p2_id]
        wires.append(f'<path d="M {x1} {y1} C {x1-40} {y1}, 700 {drop_y}, 600 {drop_y} L 400 {drop_y} C 300 {drop_y}, {x2-40} {y2}, {x2} {y2}" class="{cls}" />')

    route_under_esp("SV_LF_SIG", "ESP_L_17_9", "wire-servo", 800)
    route_under_esp("SV_RF_SIG", "ESP_L_13_18", "wire-servo", 820)
    route_under_esp("SV_LB_SIG", "ESP_L_18_10", "wire-servo", 840)
    route_s("SV_RB_SIG", "ESP_R_14_20", "wire-servo", 760) # 14 is on right side, no need to go under

    # EXT Power GND to ESP GND
    route_under_esp("PWR_GND", "ESP_L_GND_20", "wire-gnd", 880)

    svg.insert(-1, "\n".join(wires))
    svg.append('</svg>')
    
    with open('d:/esp_xiaozhi_dog-main/docs/wiring_diagram.svg', 'w', encoding='utf-8') as f:
        f.write("\n".join(svg))

if __name__ == "__main__":
    create_svg()
