import cv2
import time
import mediapipe as mp
import serial
import math

# 1. Configura a Conexão com o ESP32 (Ajustado para COM6 com parâmetros estáveis para o S3)
porta_esp32 = 'COM4'
try:
    conexao = serial.Serial(
        port=porta_esp32,
        baudrate=115200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=1,
        dsrdtr=False,
        rtscts=False
    )
    print(f"Conectado ao ESP32 na porta {porta_esp32}")
except Exception as e:
    print(f"Erro na porta serial: {e}")
    exit()

time.sleep(2) # Aguarda o ESP32 reiniciar

# 2. Configurações do MediaPipe Tasks
BaseOptions = mp.tasks.BaseOptions
HandLandmarker = mp.tasks.vision.HandLandmarker
HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions
VisionRunningMode = mp.tasks.vision.RunningMode

HAND_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),      # Polegar
    (0, 5), (5, 6), (6, 7), (7, 8),      # Dedo Indicador
    (5, 9), (9, 10), (10, 11), (11, 12), # Dedo Médio
    (9, 13), (13, 14), (14, 15), (15, 16),# Dedo Anelar
    (13, 17), (17, 18), (18, 19), (19, 20),# Dedo Mínimo
    (0, 17) # Conexão Pulso a Mínimo
]

# Configurações de calibração do gesto (Distância Euclidiana Relativa)
DIST_MIN = 0.05   # Dedos colados (Mapeia para 0)
DIST_MAX = 0.35   # Mão totalmente aberta (Mapeia para 255)

ultimo_valor_enviado = -1 # Anti-flood para variação contínua

def draw_hand_skeleton(frame, landmark_list, height, width):
    landmark_dict = {}
    for idx, landmark in enumerate(landmark_list):
        px = int(landmark.x * width)
        py = int(landmark.y * height)
        landmark_dict[idx] = (px, py)
        cv2.circle(frame, (px, py), 5, (0, 0, 255), -1) 

    for connection in HAND_CONNECTIONS:
        start_idx = connection[0]
        end_idx = connection[1]
        if start_idx in landmark_dict and end_idx in landmark_dict:
            cv2.line(frame, landmark_dict[start_idx], landmark_dict[end_idx], (255, 255, 255), 2)

def draw_dimmer_display(frame, distancia, velocidade, width):
    padding = 20
    text_x = width - 620 - padding 
    text_y_pwm =  padding

    texto_pwm = f"Velocidade do motor: {(velocidade/255*100):.2f}%"

    # Define a cor do texto baseado na intensidade
    cor_brilho = (0, int(velocidade), int(255 - velocidade))

    #cv2.putText(frame, texto_dist, (text_x, text_y_dist), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(frame, texto_pwm, (text_x, text_y_pwm), cv2.FONT_HERSHEY_SIMPLEX, 0.9, cor_brilho, 2, cv2.LINE_AA)

# Configuração do Modelo
options = HandLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=r"hand_landmarker.task"),
    running_mode=VisionRunningMode.VIDEO,
    num_hands=1
)

with HandLandmarker.create_from_options(options) as landmarker:
    cap = cv2.VideoCapture(0)
    start_time = time.time()
    
    if cap.isOpened():
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    else:
        width, height = 640, 480

    ultimo_tempo_envio = 0 

    while cap.isOpened():
        success, frame = cap.read()
        if not success:
            break

        frame = cv2.flip(frame, 1) 
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=frame_rgb)
        timestamp_ms = int((time.time() - start_time) * 1000)

        result = landmarker.detect_for_video(mp_image, timestamp_ms)

        if result.hand_landmarks:
            landmarks_da_mao = result.hand_landmarks[0]
            draw_hand_skeleton(frame, landmarks_da_mao, height, width)
            
            polegar = landmarks_da_mao[4]
            indicador = landmarks_da_mao[8]
            
            distancia = math.sqrt(
                (polegar.x - indicador.x)**2 + 
                (polegar.y - indicador.y)**2 + 
                (polegar.z - indicador.z)**2
            )
            
            if distancia <= DIST_MIN:
                velocidade = 0
            elif distancia >= DIST_MAX:
                velocidade = 255
            else:
                velocidade = int(((distancia - DIST_MIN) / (DIST_MAX - DIST_MIN)) * 255)

            tempo_atual = time.time()
            
            # Verifica se já se passaram 50ms (0.05 segundos) desde o último envio
            if (tempo_atual - ultimo_tempo_envio) >= 0.05:
                
                if abs(velocidade - ultimo_valor_enviado) >= 3:
                    conexao.write(f"{velocidade}\n".encode())
                    
                    # Atualiza os marcadores de estado
                    ultimo_valor_enviado = velocidade
                    ultimo_tempo_envio = tempo_atual
                    
                    print(f"-> valor Enviado: {velocidade}")
            
            draw_dimmer_display(frame, distancia, velocidade, width)

        cv2.imshow('MediaPipe - Controle de Brilho por Pinça', frame)
        
        if cv2.waitKey(5) & 0xFF == 27: 
            conexao.write(b"0\n") 
            print("\nCâmera interrompida com segurança.")
            break

cap.release()
cv2.destroyAllWindows()
conexao.close()
