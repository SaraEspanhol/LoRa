import time
import csv
import math
import pandas as pd

# Estrutura: cada device → lista de posições [(lat, lon, tempo), ...]
dispositivos = {}

def salvar_kml(dispositivos, arquivo="t02ABR_00.kml"):
    with open(arquivo, "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<kml xmlns="http://www.opengis.net/kml/2.2">\n')
        f.write("  <Document>\n")
        f.write("    <name>Rastreamento LoRa</name>\n")

        for device, posicoes in dispositivos.items():
            if not posicoes:
                continue

            # Último ponto
            lat, lon, tempo = posicoes[-1]
            f.write("    <Placemark>\n")
            f.write(f"      <name>{device} (última)</name>\n")
            f.write("      <Point>\n")
            f.write(f"        <coordinates>{lon},{lat},0</coordinates>\n")
            f.write("      </Point>\n")
            f.write("    </Placemark>\n")

            # Trajeto (últimas posições)
            if len(posicoes) > 1:
                coords = " ".join([f"{lon},{lat},0" for lat, lon, _ in posicoes])
                f.write("    <Placemark>\n")
                f.write(f"      <name>{device} (trilha)</name>\n")
                f.write("      <LineString>\n")
                f.write("        <tessellate>1</tessellate>\n")
                f.write(f"        <coordinates>{coords}</coordinates>\n")
                f.write("      </LineString>\n")
                f.write("    </Placemark>\n")

        f.write("  </Document>\n")
        f.write("</kml>\n")

# CSV de entrada
arquivo_csv = "t02ABR_00.csv"
offset = 0

# Define nomes de colunas manualmente
colunas = ["id_pack", "device", "lat", "lon", "hdop", "alt", "time", "receptor", "rssi"]
df = pd.read_csv(arquivo_csv, names=colunas)  # Sem cabeçalho no CSV

while True:
    try:
        with open(arquivo_csv, "r") as csvfile:
            csvfile.seek(offset)
            reader = csv.reader(csvfile)

            novas = False
            for campos in reader:
                if len(campos) != 9:
                    continue  # linha inválida

                try:
                    device = campos[1]  # ex: LORA01
                    lat = float(campos[2])
                    lon = float(campos[3])
                    tempo = campos[6]  # string "HH:MM:SS"

                    # Atualiza buffer do device
                    if device not in dispositivos:
                        dispositivos[device] = []
                    dispositivos[device].append((lat, lon, tempo))

                    # Mantém apenas últimas 50 posições
                    #if len(dispositivos[device]) > 50:
                    #    dispositivos[device].pop(0)

                    novas = True
                except Exception:
                    continue  # ignora linhas ruins

            if novas:
                salvar_kml(dispositivos)
                offset = csvfile.tell()
                print("[KML atualizado]")
            else:
                time.sleep(3)

    except FileNotFoundError:
        print(f"[ERRO] Arquivo {arquivo_csv} não encontrado. Aguardando...")
        time.sleep(3)
