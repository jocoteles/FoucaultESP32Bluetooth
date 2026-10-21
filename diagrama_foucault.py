#!/usr/bin/env python3
"""
Diagrama Espaço-Tempo (Faixas de Universo) Interativo do Pêndulo de Foucault.
Projeto: Pêndulo de Foucault - UFSCar Araras
Autor: João Teles / Assistente AI

Controle Total da Posição de Detecção:
- Slider direto 'y Detecção [cm]' para deslocar livremente a posição do círculo vermelho no instante t=0.
- Botão para alternar o sentido da velocidade no disparo (Subindo v > 0 ou Descendo v < 0).
- Sliders para tp [ms], ton [ms], Amplitude [m] e Diâmetro da Massa [m].
- Botão 'Salvar PNG' para exportar a qualquer momento.
"""

import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider, Button
from matplotlib.colors import LinearSegmentedColormap

# ==============================================================================
# 1. PARÂMETROS PADRÃO
# ==============================================================================
DEFAULT_T_MS = 3460         # Período do pêndulo [ms]
DEFAULT_TP_MS = 300         # Atraso inicial pós-detecção [ms]
DEFAULT_TON_MS = 300        # Duração do pulso ON [ms]
DEFAULT_NP = 4              # Número de pulsos por ciclo

DEFAULT_D_MASSA = 0.15      # Diâmetro da massa [m] (15 cm)
DEFAULT_D_BOBINA = 0.15     # Diâmetro físico da bobina [m] (15 cm)
DEFAULT_W_FRINGE = 0.04     # Borda difusa do campo [m] (4 cm)
DEFAULT_AMPLITUDE = 0.22    # Amplitude de oscilação métrica [m] (22 cm)
DEFAULT_Y_DET_CM = -10.5    # Posição y inicial do círculo vermelho no instante t=0 [cm]

OUTPUT_FILENAME = "diagrama_foucault.png"

# Sentido da velocidade em t=0 (1 para v > 0, -1 para v < 0)
velocity_direction = 1

# ==============================================================================
# 2. FUNÇÃO DE CÁLCULO E MODELAGEM FÍSICA
# ==============================================================================
def compute_dynamics(tp_ms, ton_ms, n_pulses, amp_val, d_massa_val, y_det_cm, v_dir):
    T = DEFAULT_T_MS / 1000.0
    tp = tp_ms / 1000.0
    ton = ton_ms / 1000.0
    toff = (DEFAULT_T_MS // 2 - ton_ms) / 1000.0
    omega = 2.0 * np.pi / T
    
    R_massa = d_massa_val / 2.0
    R_bobina = DEFAULT_D_BOBINA / 2.0
    R_campo_max = R_bobina + DEFAULT_W_FRINGE
    
    # Conversão de y_det de cm para metros, limitado pela amplitude atual
    y_det = np.clip(y_det_cm / 100.0, -amp_val * 0.999, amp_val * 0.999)
    
    # Cálculo da fase inicial phi0 a partir da posição y_det e do sentido v_dir:
    # y(t) = amp * sin(omega * t + phi0)
    # v(t) = amp * omega * cos(omega * t + phi0)
    base_asin = np.arcsin(y_det / amp_val)
    if v_dir > 0:
        phi0 = base_asin  # cos(phi0) > 0 -> velocidade positiva (subindo)
    else:
        phi0 = np.pi - base_asin  # cos(phi0) < 0 -> velocidade negativa (descendo)
        
    # Pulsos do ESP32:
    # delay(tp); for (i = 0; i < Np; i++) { delay(toff); LIGA; delay(ton); }
    pulse_intervals = []
    t_cursor = 0.0 + tp
    for _ in range(int(n_pulses)):
        t_start = t_cursor + toff
        t_end = t_start + ton
        pulse_intervals.append((t_start, t_end))
        t_cursor = t_end
        
    t_max = pulse_intervals[-1][1] + 1.2
    t_pts = np.linspace(0, t_max, 3000)
    
    y_center = amp_val * np.sin(omega * t_pts + phi0)
    v_center = amp_val * omega * np.cos(omega * t_pts + phi0)
    y_top = y_center + R_massa
    y_bot = y_center - R_massa
    
    # Métricas de aceleração vs frenagem
    dt = t_pts[1] - t_pts[0]
    total_acc_time = 0.0
    total_ret_time = 0.0
    
    for t_start, t_end in pulse_intervals:
        mask = (t_pts >= t_start) & (t_pts <= t_end)
        y_sub_top = y_top[mask]
        y_sub_bot = y_bot[mask]
        y_sub_c = y_center[mask]
        v_sub_c = v_center[mask]
        
        y_inter_top = np.minimum(y_sub_top, R_campo_max)
        y_inter_bot = np.maximum(y_sub_bot, -R_campo_max)
        valid = y_inter_top > y_inter_bot
        approaching = (y_sub_c * v_sub_c) < 0
        
        total_acc_time += np.sum(valid & approaching) * dt
        total_ret_time += np.sum(valid & (~approaching)) * dt
        
    total_interact = total_acc_time + total_ret_time
    frac_acc = (total_acc_time / total_interact * 100.0) if total_interact > 0 else 0
    frac_ret = (total_ret_time / total_interact * 100.0) if total_interact > 0 else 0
    
    return {
        "T": T, "tp": tp, "ton": ton, "toff": toff,
        "R_massa": R_massa, "R_bobina": R_bobina, "R_campo_max": R_campo_max,
        "y_det": y_det, "v_dir": v_dir, "pulse_intervals": pulse_intervals,
        "t_max": t_max, "t_pts": t_pts,
        "y_center": y_center, "v_center": v_center, "y_top": y_top, "y_bot": y_bot,
        "total_acc_time": total_acc_time, "total_ret_time": total_ret_time,
        "frac_acc": frac_acc, "frac_ret": frac_ret
    }

# ==============================================================================
# 3. INTERFACE GRÁFICA COM SLIDERS E ATUALIZAÇÃO REATIVA
# ==============================================================================
fig, ax = plt.subplots(figsize=(15, 8.5), dpi=120)
plt.subplots_adjust(bottom=0.30, right=0.80, top=0.91)

cmap_field = LinearSegmentedColormap.from_list("fringe_map", ["#ffffff", "#e3f2fd", "#90caf9", "#42a5f5"])

def redraw_plot(data, tp_ms, ton_ms, amp_val, d_massa_val, y_det_cm):
    ax.clear()
    
    t_pts = data["t_pts"]
    t_max = data["t_max"]
    R_bobina = data["R_bobina"]
    R_campo_max = data["R_campo_max"]
    
    # 1. Campo magnético da bobina no eixo Y
    y_grid = np.linspace(-R_campo_max * 1.5, R_campo_max * 1.5, 300)
    B_profile = np.zeros_like(y_grid)
    for idx, y_val in enumerate(y_grid):
        dist = abs(y_val)
        if dist <= R_bobina:
            B_profile[idx] = 1.0
        elif dist <= R_campo_max:
            B_profile[idx] = 0.5 * (1.0 + np.cos(np.pi * (dist - R_bobina) / DEFAULT_W_FRINGE))
        else:
            B_profile[idx] = 0.0

    B_mesh = np.tile(B_profile[:, np.newaxis], (1, len(t_pts)))
    ax.imshow(B_mesh, extent=[0, t_max, y_grid[0], y_grid[-1]],
              origin="lower", aspect="auto", cmap=cmap_field, alpha=0.55, zorder=1)

    # Linhas de referência da bobina
    ax.axhline(R_bobina, color="#1565c0", linestyle="--", linewidth=1.2, alpha=0.8,
               label=f"Borda da Bobina (±{R_bobina*100:.1f} cm)")
    ax.axhline(-R_bobina, color="#1565c0", linestyle="--", linewidth=1.2, alpha=0.8)
    ax.axhline(R_campo_max, color="#90caf9", linestyle=":", linewidth=1.0, alpha=0.7,
               label=f"Campo Difuso (±{R_campo_max*100:.1f} cm)")
    ax.axhline(-R_campo_max, color="#90caf9", linestyle=":", linewidth=1.0, alpha=0.7)
    ax.axhline(0.0, color="gray", linestyle="-.", linewidth=0.8, alpha=0.7, label="Centro / Equilíbrio (y = 0)")

    # 2. Pulsos do Eletroímã (Faixas verticais)
    for idx, (t_start, t_end) in enumerate(data["pulse_intervals"]):
        lbl = "Eletroímã LIGADO (Von)" if idx == 0 else None
        ax.axvspan(t_start, t_end, color="#ffe082", alpha=0.45, zorder=2, label=lbl)
        ax.text((t_start + t_end) / 2.0, amp_val * 1.22, f"Pulso #{idx+1}",
                color="#d84315", fontsize=8.5, fontweight="bold", ha="center", va="bottom", zorder=6)

    # 3. Faixa de Universo da Massa
    ax.fill_between(t_pts, data["y_bot"], data["y_top"], color="#37474f", alpha=0.30,
                    label=f"Massa do Pêndulo (D={d_massa_val*100:.0f} cm)", zorder=3)
    ax.plot(t_pts, data["y_center"], color="#212121", linestyle="-", linewidth=1.6,
            label="Trajetória do CM", zorder=4)

    # Ponto de detecção no instante t=0
    sentido_str = "Subindo (v>0)" if data["v_dir"] > 0 else "Descendo (v<0)"
    ax.scatter([0.0], [data["y_det"]], color="#e53935", s=85, zorder=7,
               label=f"Detecção RLC: y={data['y_det']*100:.1f} cm ({sentido_str})")

    # 4. Sobreposição Tripla (Verde = Aceleração / Vermelho = Frenagem)
    for idx, (t_start, t_end) in enumerate(data["pulse_intervals"]):
        mask = (t_pts >= t_start) & (t_pts <= t_end)
        t_sub = t_pts[mask]
        y_sub_top = data["y_top"][mask]
        y_sub_bot = data["y_bot"][mask]
        y_sub_c = data["y_center"][mask]
        v_sub_c = data["v_center"][mask]

        y_inter_top = np.minimum(y_sub_top, R_campo_max)
        y_inter_bot = np.maximum(y_sub_bot, -R_campo_max)
        valid = y_inter_top > y_inter_bot

        if np.any(valid):
            approaching = (y_sub_c * v_sub_c) < 0
            mask_acc = valid & approaching
            if np.any(mask_acc):
                lbl_acc = "Sobreposição Aceleradora" if idx == 0 else None
                ax.fill_between(t_sub[mask_acc], y_inter_bot[mask_acc], y_inter_top[mask_acc],
                                hatch="//", facecolor="#66bb6a", edgecolor="#2e7d32", alpha=0.7,
                                zorder=5, label=lbl_acc)

            mask_ret = valid & (~approaching)
            if np.any(mask_ret):
                lbl_ret = "Sobreposição Retardadora (Frenagem)" if idx == 0 else None
                ax.fill_between(t_sub[mask_ret], y_inter_bot[mask_ret], y_inter_top[mask_ret],
                                hatch="\\\\", facecolor="#ef5350", edgecolor="#c62828", alpha=0.7,
                                zorder=5, label=lbl_ret)

    status_str = f"Útil: {data['frac_acc']:.0f}% Acelerador | {data['frac_ret']:.0f}% Frenagem"
    ax.set_title(f"Diagrama Interativo do Pêndulo de Foucault (UFSCar Araras)\n"
                 f"tp={tp_ms} ms | ton={ton_ms} ms | Amp={amp_val*100:.0f} cm | y_det={data['y_det']*100:.1f} cm | [{status_str}]",
                 fontsize=12, fontweight="bold", pad=12)
    ax.set_xlabel("Tempo t [segundos]", fontsize=10.5, fontweight="bold")
    ax.set_ylabel("Posição no Eixo de Oscilação y [metros]", fontsize=10.5, fontweight="bold")

    ax.set_xlim(0, t_max)
    ax.set_ylim(-amp_val * 1.4, amp_val * 1.4)
    ax.grid(True, linestyle=":", alpha=0.6, zorder=0)
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), frameon=True, fontsize=8.5)

# ==============================================================================
# 4. INSTANCIAÇÃO DOS SLIDERS E BOTÕES
# ==============================================================================
slider_color = "#b0bec5"
ax_tp     = plt.axes([0.15, 0.20, 0.40, 0.022], facecolor=slider_color)
ax_ton    = plt.axes([0.15, 0.16, 0.40, 0.022], facecolor=slider_color)
ax_amp    = plt.axes([0.15, 0.12, 0.40, 0.022], facecolor=slider_color)
ax_dmassa = plt.axes([0.15, 0.08, 0.40, 0.022], facecolor=slider_color)
ax_ydet   = plt.axes([0.15, 0.04, 0.40, 0.022], facecolor=slider_color)

s_tp     = Slider(ax_tp, 'tp [ms]', 0, 800, valinit=DEFAULT_TP_MS, valstep=10)
s_ton    = Slider(ax_ton, 'ton [ms]', 50, 600, valinit=DEFAULT_TON_MS, valstep=10)
s_amp    = Slider(ax_amp, 'Amplitude [m]', 0.10, 0.50, valinit=DEFAULT_AMPLITUDE, valstep=0.01)
s_dmassa = Slider(ax_dmassa, 'D Massa [m]', 0.08, 0.30, valinit=DEFAULT_D_MASSA, valstep=0.01)
s_ydet   = Slider(ax_ydet, 'y Detecção [cm]', -25.0, 25.0, valinit=DEFAULT_Y_DET_CM, valstep=0.5)

# Botão para alternar o sentido da velocidade em t=0
ax_btn_dir = plt.axes([0.62, 0.16, 0.16, 0.045])
btn_dir = Button(ax_btn_dir, 'Sentido: Subindo (v>0)', color='#c8e6c9', hovercolor='#a5d6a7')

# Botão para salvar PNG
ax_btn_save = plt.axes([0.62, 0.10, 0.16, 0.045])
btn_save = Button(ax_btn_save, 'Salvar PNG', color='#bbdefb', hovercolor='#90caf9')

# Caixa de texto de diagnóstico ao lado dos sliders
ax_info = plt.axes([0.62, 0.03, 0.22, 0.05])
ax_info.axis('off')
info_text = ax_info.text(0.0, 0.5, "", fontsize=9.5, fontweight="bold", va="center")

def update(val=None):
    tp_val = int(s_tp.val)
    ton_val = int(s_ton.val)
    amp_val = float(s_amp.val)
    dmassa_val = float(s_dmassa.val)
    ydet_val = float(s_ydet.val)
    
    data = compute_dynamics(tp_val, ton_val, DEFAULT_NP, amp_val, dmassa_val, ydet_val, velocity_direction)
    redraw_plot(data, tp_val, ton_val, amp_val, dmassa_val, ydet_val)
    
    cor = "#2e7d32" if data['frac_ret'] <= 15 else ("#e65100" if data['frac_ret'] <= 35 else "#c62828")
    info_text.set_text(f"Aceleração: {data['frac_acc']:.1f}%\nFrenagem:   {data['frac_ret']:.1f}%")
    info_text.set_color(cor)
    fig.canvas.draw_idle()

def toggle_direction(event=None):
    global velocity_direction
    velocity_direction = -velocity_direction
    if velocity_direction > 0:
        btn_dir.label.set_text('Sentido: Subindo (v>0)')
        btn_dir.color = '#c8e6c9'
    else:
        btn_dir.label.set_text('Sentido: Descendo (v<0)')
        btn_dir.color = '#ffe0b2'
    update()

def save_current_png(event=None):
    plt.savefig(OUTPUT_FILENAME, dpi=200)
    print(f"[OK] Gráfico salvo com sucesso em: {OUTPUT_FILENAME}")

btn_dir.on_clicked(toggle_direction)
btn_save.on_clicked(save_current_png)
s_tp.on_changed(update)
s_ton.on_changed(update)
s_amp.on_changed(update)
s_dmassa.on_changed(update)
s_ydet.on_changed(update)

# Execução inicial
init_data = compute_dynamics(DEFAULT_TP_MS, DEFAULT_TON_MS, DEFAULT_NP, DEFAULT_AMPLITUDE, DEFAULT_D_MASSA, DEFAULT_Y_DET_CM, velocity_direction)
redraw_plot(init_data, DEFAULT_TP_MS, DEFAULT_TON_MS, DEFAULT_AMPLITUDE, DEFAULT_D_MASSA, DEFAULT_Y_DET_CM)
save_current_png()

print("=" * 60)
print(f"DIAGNÓSTICO INICIAL (tp={DEFAULT_TP_MS} ms, ton={DEFAULT_TON_MS} ms, y_det={DEFAULT_Y_DET_CM} cm):")
print(f" - Aceleração: {init_data['frac_acc']:.1f}% | Frenagem: {init_data['frac_ret']:.1f}%")
print(f" - Imagem salva: {OUTPUT_FILENAME}")
print("=" * 60)

if sys.flags.interactive == 0 and "pytest" not in sys.modules:
    try:
        if plt.get_backend().lower() != 'agg':
            plt.show()
    except Exception:
        pass
