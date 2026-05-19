\documentclass{article}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage[german]{babel}
\usepackage{geometry}
\geometry{a4paper, margin=1in}
\usepackage{tcolorbox}
\usepackage{listings}
\usepackage{xcolor}
\usepackage{hyperref}

% Farbdefinitionen für Code-Highlighting
\definecolor{codebg}{gray}{0.95}
\definecolor{commentgreen}{rgb}{0,0.6,0}
\definecolor{keywordblue}{rgb}{0,0,0.8}
\definecolor{stringstring}{rgb}{0.58,0,0.82}

\lstset{
    backgroundcolor=\color{codebg},
    basicstyle={\small\ttfamily},
    breakatwhitespace=false,
    breaklines=true,
    commentstyle=\color{commentgreen},
    keywordstyle=\color{keywordblue},
    stringstyle=\color{stringstring},
    frame=single,
    keepspaces=true,
    numbers=none,
    showspaces=false,
    showstringspaces=false,
    showtabs=false,
    tabsize=2
}

\title{ESPHome Zeico / Dream Maker Fan Integration}
\author{}
\date{Mai 2026}

\begin{document}

\maketitle

This repository provides an unofficial, native ESPHome external component for local, cloud-free control of \textbf{Zeico / Dream Maker (Zhejiang Dream Maker Intelligent Environmental Technology Co., Ltd.)} smart fans. 

By replacing the original Tuya-based Wi-Fi firmware with ESPHome, this component communicates directly with the fan's hardware MCU over a wired serial connection (UART).

\begin{tcolorbox}[colback=red!5!white,colframe=red!75!black,title=\textbf{Beta / Untested Status}]
    This integration has been reverse-engineered and written based on raw UART protocol captures, firmware dumps, and code generation. It \textbf{has not yet been tested in the wild} on live hardware. Use with caution, and be prepared to monitor logs during your first flash!
\end{tcolorbox}

\section*{🚀 Features (Decoded via UART Protocol)}

The component implements a custom state machine to handle the proprietary \texttt{FA CE} magic header protocol at \texttt{9600 baud}:

\begin{itemize}
    \item \textbf{Full Fan Control:} Power (ON/OFF), Speed percentage (1--100\%), and Mode selection (\texttt{direct}, \texttt{natural}/wind, \texttt{smart}/night).
    \item \textbf{Hardware Switches:} Core toggles for Oscillation, Child Lock, Status LED, and Buzzer Sounds.
    \item \textbf{Onboard Sensors:} Real-time feedback for Room Temperature, Relative Humidity, and current Oscillation/Roll Angle.
\end{itemize}

\section*{🛠️ Repository Structure}

To use this as an \texttt{external\_component}, ensure your repository directory matches the standard ESPHome layout:

\begin{lstlisting}[language=sh]
esphome-zeico-fan/
├── LICENSE
├── README.md
├── dm_fan.yaml            # Example configuration for your ESP32 device
└── components/
    └── dm_fan/
        ├── __init__.py    # Python configuration and component wiring
        ├── dm_fan.h       # C++ core state machine and UART parsing logic
        └── fan.py         # Empty placeholder required by ESPHome architecture
\end{lstlisting}

\section*{📦 How to Integrate}

Once this repository is live on GitHub, anyone can easily pull the component directly into their ESPHome configuration without downloading files manually:

\begin{lstlisting}[language=XML]
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_GITHUB_USERNAME/esphome-zeico-fan
    components: [ dm_fan ]

uart:
  id: uart_mcu
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

dm_fan:
  id: fan_hub
  uart_id: uart_mcu

# ... reference your switches, sensors, and numbers as shown in dm_fan.yaml
\end{lstlisting}

\section*{🤝 Help Wanted \& Unconfirmed Details}

Since this project is in an experimental stage, early adopters and testers are highly welcome! 

Specifically, the following data fields need live validation via the ESPHome \texttt{DEBUG} logs:
\begin{enumerate}
    \item \textbf{Temperature \& Humidity Bytes:} Currently mapped to bytes 12 and 15 based on plausibility. We need verification if these match actual ambient conditions.
    \item \textbf{Peripheral Switches:} The exact byte triggers for \textit{Child Lock}, \textit{LED Lights}, and \textit{Buzzer Sounds} are built according to firmware tables but require physical testing on the fan unit to confirm they react correctly.
\end{enumerate}

\subsection*{🔴 Note on the Bluetooth Remote Control}
Initial attempts to spoof the physical BLE remote control (\texttt{MAC 84:0A:10:78:19:33}) revealed that the remote utilizes the \textbf{Tuya Beacon Protocol} with secure \textbf{XXTEA encryption}. It relies on an ephemeral, encrypted challenge-response handshake during pairing. Because of this security layer, \textbf{the stock physical Bluetooth remote is entirely ignored by this component}. Control is intended solely via Home Assistant or alternative smart switches (Zigbee, etc.).

If you test this on your fan, please \textbf{open an Issue} or submit a \textbf{Pull Request} with your log readouts so we can refine the byte mapping!

\end{document}
