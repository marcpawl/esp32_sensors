#!/bin/bash -ex

cd $(dirname $0)

mkdir -p ${HOME}/.config/autostart
cat<<EOF > ${HOME}/.config/autostart/deepseek_alert.desktop
[Desktop Entry]
Type=Application
Name=DeepSeek Alert Monitor
Comment=Tracks DeepSeek peak pricing windows
Exec=$(pwd)/deepseek_alert.sh
Terminal=false
X-GNOME-Autostart-enabled=true
Categories=Utility;Development;
EOF

chmod +x ${HOME}/.config/autostart/deepseek_alert.desktop
