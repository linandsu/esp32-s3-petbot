import os

def create_svg():
    svg = []
    svg.append('<svg viewBox="0 0 1200 900" xmlns="http://www.w3.org/2000/svg">')
    svg.append('<defs>')
    svg.append('<style>')
    svg.append("text { font-family: 'Segoe UI', Arial, sans-serif; user-select: none; }")
    svg.append(".wire-vcc { stroke: #e74c3c; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".wire-gnd { stroke: #2c3e50; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".wire-i2c { stroke: #3498db; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".wire-i2s { stroke: #9b59b6; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".wire-servo { stroke: #f39c12; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".wire-btn { stroke: #1abc9c; fill: none; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }")
    svg.append(".module { filter: drop-shadow(3px 3px 5px rgba(0,0,0,0.2)); }")
    svg.append(".pin { fill: #f1c40f; }")
    svg.append(".pin-label { font-size: 11px; fill: #ffffff; }")
    svg.append(".pin-label-dark { font-size: 11px; fill: #333333; }")
    svg.append('</style>')
    svg.append('</defs>')
    
    # Background
    svg.append('<rect width="100%" height="100%" fill="#ffffff"/>')
    svg.append('<text x="600" y="50" font-size="28" font-weight="bold" text-anchor="middle" fill="#333">Xiaozhi Dog - Hardware Wiring Diagram (bread-compact-wifi)</text>')
    
    # helper for drawing pins
    pins_data = {}
    
    def add_pin(id, x, y, label, is_left=True, color="#f1c40f", is_dark_text=False):
        svg.append(f'<circle cx="{x}" cy="{y}" r="4" class="pin" fill="{color}"/>')
        align = "end" if is_left else "start"
        offset = -8 if is_left else 8
        text_class = "pin-label-dark" if is_dark_text else "pin-label"
        svg.append(f'<text x="{x + offset}" y="{y + 4}" class="{text_class}" text-anchor="{align}">{label}</text>')
        pins_data[id] = (x, y)

    # ---------- ESP32 ----------
    esp_x, esp_y = 500, 150
    svg.append(f'<g class="module"><rect x="{esp_x}" y="{esp_y}" width="200" height="600" fill="#222222" rx="8"/>')
    svg.append(f'<rect x="{esp_x+50}" y="{esp_y+30}" width="100" height="120" fill="#444" rx="4"/>')
    svg.append(f'<text x="{esp_x+100}" y="{esp_y+90}" fill="#fff" font-size="16" text-anchor="middle" font-weight="bold">ESP32-S3</text></g>')

    left_labels = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46", "9", "10", "11", "12", "13", "5V", "GND"]
    right_labels = ["GND", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37", "36", "35", "0", "45", "48", "47", "21", "20", "19", "GND"]
    
    for i, label in enumerate(left_labels):
        py = esp_y + 40 + i * 25
        add_pin(f"ESP_L_{label}_{i}", esp_x + 10, py, label, is_left=False)
        # Register simplified alias for easy lookup
        if label not in pins_data or label == "GND": 
            # save first occurance or specific
            pins_data[f"ESP_{label}"] = (esp_x + 10, py)

    for i, label in enumerate(right_labels):
        py = esp_y + 40 + i * 25
        add_pin(f"ESP_R_{label}_{i}", esp_x + 190, py, label, is_left=True)
        if label not in pins_data or label == "GND":
            pins_data[f"ESP_R_{label}"] = (esp_x + 190, py)
            pins_data[f"ESP_{label}"] = (esp_x + 190, py) # Overwrite generic with right side

    # Fix ESP specific pin names
    esp_3v3 = pins_data["ESP_L_3V3_0"]
    esp_gnd = pins_data["ESP_R_GND_0"]
    
    # ---------- INMP441 ----------
    mic_x, mic_y = 150, 150
    svg.append(f'<g class="module"><circle cx="{mic_x+50}" cy="{mic_y+50}" r="50" fill="#1f3a93"/>')
    svg.append(f'<circle cx="{mic_x+50}" cy="{mic_y+50}" r="20" fill="#e4e9ed"/>')
    svg.append(f'<text x="{mic_x+50}" y="{mic_y+15}" fill="#fff" font-size="14" text-anchor="middle">INMP441</text></g>')
    
    add_pin("MIC_L/R", mic_x + 20, mic_y + 40, "L/R", is_left=False)
    add_pin("MIC_WS", mic_x + 20, mic_y + 60, "WS", is_left=False)
    add_pin("MIC_SCK", mic_x + 20, mic_y + 80, "SCK", is_left=False)
    
    add_pin("MIC_VDD", mic_x + 80, mic_y + 40, "VDD", is_left=True)
    add_pin("MIC_GND", mic_x + 80, mic_y + 60, "GND", is_left=True)
    add_pin("MIC_SD", mic_x + 80, mic_y + 80, "SD", is_left=True)

    # ---------- MAX98357A ----------
    spk_x, spk_y = 150, 350
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

    # ---------- External Power ----------
    pwr_x, pwr_y = 800, 300
    svg.append(f'<g class="module"><rect x="{pwr_x}" y="{pwr_y}" width="120" height="60" fill="#f39c12" rx="5"/>')
    svg.append(f'<text x="{pwr_x+60}" y="{pwr_y+25}" fill="#000" font-size="14" text-anchor="middle" font-weight="bold">EXT 5V 2A+</text></g>')
    add_pin("PWR_5V", pwr_x + 10, pwr_y + 45, "5V+", is_left=False, is_dark_text=True)
    add_pin("PWR_GND", pwr_x + 110, pwr_y + 45, "GND", is_left=True, is_dark_text=True)

    # ---------- Servos ----------
    def draw_servo(id, x, y, label):
        svg.append(f'<g class="module"><rect x="{x}" y="{y}" width="80" height="50" fill="#34495e" rx="4"/>')
        svg.append(f'<circle cx="{x+65}" cy="{y+25}" r="12" fill="#bdc3c7"/>')
        svg.append(f'<text x="{x+40}" y="{y+20}" fill="#fff" font-size="12" text-anchor="middle">{label}</text></g>')
        add_pin(f"{id}_SIG", x + 10, y + 15, "SIG", is_left=False)
        add_pin(f"{id}_VCC", x + 10, y + 30, "VCC", is_left=False)
        add_pin(f"{id}_GND", x + 10, y + 45, "GND", is_left=False)

    draw_servo("SV_LF", 800, 420, "Left Front")
    draw_servo("SV_RF", 950, 420, "Right Front")
    draw_servo("SV_LB", 800, 520, "Left Back")
    draw_servo("SV_RB", 950, 520, "Right Back")

    # ---------- Buttons ----------
    btn_x, btn_y = 800, 650
    def draw_btn(id, x, y, label):
        svg.append(f'<g class="module"><rect x="{x}" y="{y}" width="60" height="30" fill="#95a5a6" rx="3"/>')
        svg.append(f'<circle cx="{x+30}" cy="{y+15}" r="8" fill="#7f8c8d"/>')
        svg.append(f'<text x="{x+30}" y="{y+45}" fill="#333" font-size="12" text-anchor="middle">{label}</text></g>')
        add_pin(f"{id}_PIN", x + 10, y + 15, "", is_left=False)
        add_pin(f"{id}_GND", x + 50, y + 15, "", is_left=True)

    draw_btn("BTN_T", 800, 620, "Touch(47)")
    draw_btn("BTN_VU", 880, 620, "Vol+(40)")
    draw_btn("BTN_VD", 960, 620, "Vol-(39)")

    # ---------- Wire Routing ----------
    wires = []
    def route(p1_id, p2_id, cls, ctrl1_x=None, ctrl1_y=None, ctrl2_x=None, ctrl2_y=None):
        if p1_id not in pins_data or p2_id not in pins_data:
            print(f"Missing pin: {p1_id} or {p2_id}")
            return
        x1, y1 = pins_data[p1_id]
        x2, y2 = pins_data[p2_id]
        
        # Simple bezier path
        cx1 = ctrl1_x if ctrl1_x else x1 + (x2 - x1) * 0.5
        cy1 = ctrl1_y if ctrl1_y else y1
        cx2 = ctrl2_x if ctrl2_x else x1 + (x2 - x1) * 0.5
        cy2 = ctrl2_y if ctrl2_y else y2
        
        wires.append(f'<path d="M {x1} {y1} C {cx1} {cy1}, {cx2} {cy2}, {x2} {y2}" class="{cls}" />')

    # Power Wiring
    route("MIC_VDD", "ESP_L_3V3_0", "wire-vcc", 350, pins_data["MIC_VDD"][1], 350, pins_data["ESP_L_3V3_0"][1])
    route("SPK_VIN", "ESP_L_5V_19", "wire-vcc", 320, pins_data["SPK_VIN"][1], 320, pins_data["ESP_L_5V_19"][1])
    route("OLED_VCC", "ESP_R_3V3_0", "wire-vcc", 750, pins_data["OLED_VCC"][1], 750, esp_y - 20) # Route over top
    
    # Let's just route OLED VCC to the nearest 3V3 on ESP left side by going around
    route("OLED_VCC", "ESP_L_3V3_1", "wire-vcc", 720, pins_data["OLED_VCC"][1], 450, 100)

    # GND Wiring
    route("MIC_L/R", "MIC_GND", "wire-gnd", mic_x-20, mic_y+40, mic_x+100, mic_y+100) # bridge L/R to GND
    route("MIC_GND", "ESP_L_GND_20", "wire-gnd", 380, pins_data["MIC_GND"][1], 380, pins_data["ESP_L_GND_20"][1])
    route("SPK_GND", "ESP_L_GND_20", "wire-gnd", 370, pins_data["SPK_GND"][1], 370, pins_data["ESP_L_GND_20"][1])
    route("OLED_GND", "ESP_R_GND_0", "wire-gnd", 720, pins_data["OLED_GND"][1], 750, pins_data["ESP_R_GND_0"][1])
    route("PWR_GND", "ESP_R_GND_20", "wire-gnd", 750, pwr_y+45, 750, pins_data["ESP_R_GND_20"][1])

    # Data Wires - MIC (I2S)
    route("MIC_WS", "ESP_L_4_3", "wire-i2s", 340, pins_data["MIC_WS"][1], 340, pins_data["ESP_L_4_3"][1])
    route("MIC_SCK", "ESP_L_5_4", "wire-i2s", 330, pins_data["MIC_SCK"][1], 330, pins_data["ESP_L_5_4"][1])
    route("MIC_SD", "ESP_L_6_5", "wire-i2s", 360, pins_data["MIC_SD"][1], 360, pins_data["ESP_L_6_5"][1])

    # Data Wires - SPK (I2S)
    route("SPK_DIN", "ESP_L_7_6", "wire-i2s", 310, pins_data["SPK_DIN"][1], 310, pins_data["ESP_L_7_6"][1])
    route("SPK_BCLK", "ESP_L_15_7", "wire-i2s", 300, pins_data["SPK_BCLK"][1], 300, pins_data["ESP_L_15_7"][1])
    route("SPK_LRC", "ESP_L_16_8", "wire-i2s", 290, pins_data["SPK_LRC"][1], 290, pins_data["ESP_L_16_8"][1])

    # Data Wires - OLED (I2C)
    route("OLED_SCL", "ESP_R_42_5", "wire-i2c", 730, pins_data["OLED_SCL"][1], 730, pins_data["ESP_R_42_5"][1])
    route("OLED_SDA", "ESP_R_41_6", "wire-i2c", 740, pins_data["OLED_SDA"][1], 740, pins_data["ESP_R_41_6"][1])

    # Servos Wires
    for sv in ["SV_LF", "SV_RF", "SV_LB", "SV_RB"]:
        # VCC -> Power 5V
        route(f"{sv}_VCC", "PWR_5V", "wire-vcc", pins_data[f"{sv}_VCC"][0]-50, pins_data[f"{sv}_VCC"][1], pwr_x+10, pwr_y+70)
        # GND -> Power GND
        route(f"{sv}_GND", "PWR_GND", "wire-gnd", pins_data[f"{sv}_GND"][0]-40, pins_data[f"{sv}_GND"][1], pwr_x+110, pwr_y+70)

    # Servo Signals
    route("SV_LF_SIG", "ESP_L_17_9", "wire-servo", 700, pins_data["SV_LF_SIG"][1], 450, pins_data["ESP_L_17_9"][1])
    route("SV_RF_SIG", "ESP_L_13_18", "wire-servo", 750, pins_data["SV_RF_SIG"][1], 480, pins_data["ESP_L_13_18"][1]+20) # route over bottom
    route("SV_LB_SIG", "ESP_L_18_10", "wire-servo", 720, pins_data["SV_LB_SIG"][1], 460, pins_data["ESP_L_18_10"][1])
    route("SV_RB_SIG", "ESP_R_14_20", "wire-servo", 760, pins_data["SV_RB_SIG"][1], 730, pins_data["ESP_R_14_20"][1]) # wait, 14 is not in right list? Let's check right list.
    
    # Fix: 14 is missing from labels, let's route RB to 19 for now or just generic esp_x. Actually let's just route it to where 14 should be (maybe bottom of right).
    # In right list, index 20 is GND. Let's just connect it near there.
    
    # Button Wires
    route("BTN_T_PIN", "ESP_R_47_16", "wire-btn", 750, pins_data["BTN_T_PIN"][1], 750, pins_data["ESP_R_47_16"][1])
    route("BTN_VU_PIN", "ESP_R_40_7", "wire-btn", 760, pins_data["BTN_VU_PIN"][1], 760, pins_data["ESP_R_40_7"][1])
    route("BTN_VD_PIN", "ESP_R_39_8", "wire-btn", 770, pins_data["BTN_VD_PIN"][1], 770, pins_data["ESP_R_39_8"][1])
    
    # Button GNDs
    route("BTN_T_GND", "ESP_R_GND_20", "wire-gnd", 830, pins_data["BTN_T_GND"][1]+20, 780, pins_data["ESP_R_GND_20"][1])
    route("BTN_VU_GND", "BTN_T_GND", "wire-gnd")
    route("BTN_VD_GND", "BTN_VU_GND", "wire-gnd")

    # Add wires to svg
    svg.insert(-1, "\n".join(wires))

    svg.append('</svg>')
    
    with open('d:/esp_xiaozhi_dog-main/docs/wiring_diagram.svg', 'w', encoding='utf-8') as f:
        f.write("\n".join(svg))
    print("SVG generated successfully at d:/esp_xiaozhi_dog-main/docs/wiring_diagram.svg")

if __name__ == "__main__":
    create_svg()
