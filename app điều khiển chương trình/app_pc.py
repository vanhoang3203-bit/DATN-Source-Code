import sys, socket, cv2, struct, os, shutil, numpy as np
from PyQt5 import QtWidgets, uic, QtCore, QtGui
from PyQt5.QtCore import QTimer, QDateTime, pyqtSignal, QObject, QThread, Qt
from PyQt5.QtGui import QImage, QPixmap, QPen, QColor, QFont
from openpyxl import Workbook, load_workbook
from openpyxl.drawing.image import Image as OpenpyxlImage

# --- LUỒNG XỬ LÝ EXPORT DỮ LIỆU ---
class ExportWorker(QObject):
    finished = pyqtSignal(str)
    def __init__(self, excel_file):
        super().__init__()
        self.excel_file = excel_file

    def run(self):
        results = []
        # Xử lý khoảng trắng trong đường dẫn (ví dụ: NGUYEN HOANG)
        excel_path = f'"{self.excel_file}"'
        
        try:
            # Xuất Google Drive qua Rclone
            cmd = f'rclone copy {excel_path} gdrive:DoAn_Lidar/'
            res = os.system(cmd)
            if res == 0:
                results.append("✅ Google Drive: OK")
            else:
                results.append("❌ Google Drive: Lỗi Rclone")
        except:
            results.append("❌ Google Drive: Lỗi thực thi")

        # Sao chép sang USB
        usb_found = False
        import string
        drives = [f"{d}:\\" for d in string.ascii_uppercase if d not in ['A', 'B', 'C']]
        for d in drives:
            if os.path.exists(d):
                try:
                    shutil.copy(self.excel_file, os.path.join(d, "BaoCao_KetQua.xlsx"))
                    results.append(f"✅ USB ({d}): OK")
                    usb_found = True; break
                except: pass
        if not usb_found: results.append("❌ USB: Không tìm thấy")
        
        self.finished.emit("\n".join(results))

# --- LUỒNG NHẬN VIDEO VÀ CHẠY AI (ĐẶC TRỊ DELAY) ---
class CameraWorker(QThread):
    frame_ready = pyqtSignal(np.ndarray, list)
    
    def __init__(self, ip, net, ai_ready):
        super().__init__()
        self.ip = ip
        self.net = net
        self.ai_ready = ai_ready
        self.running = True
        self.frame_count = 0
        self.last_detections = [] 
        self.CLASSES = ["background", "aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat", "chair", "cow", "diningtable", "dog", "horse", "motorbike", "person", "pottedplant", "sheep", "sofa", "train", "tvmonitor"]

    def run(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((self.ip, 5555))
            while self.running:
                size_raw = sock.recv(struct.calcsize(">L"))
                if not size_raw: break
                size = struct.unpack(">L", size_raw)[0]
                data = b""
                while len(data) < size:
                    packet = sock.recv(size - len(data))
                    if not packet: break
                    data += packet
                
                frame = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
                if frame is None: continue
                
                h, w = frame.shape[:2]
                found = []
                self.frame_count += 1

                # CHỈ CHẠY AI Ở KHUNG HÌNH CHẴN ĐỂ GIẢM LAG
                if self.ai_ready and (self.frame_count % 2 == 0):
                    blob = cv2.dnn.blobFromImage(cv2.resize(frame, (300, 300)), 0.007843, (300, 300), 127.5)
                    self.net.setInput(blob)
                    detections = self.net.forward()
                    self.last_detections = []
                    
                    for i in range(detections.shape[2]):
                        if detections[0, 0, i, 2] > 0.5:
                            idx = int(detections[0, 0, i, 1])
                            lbl = self.CLASSES[idx].upper()
                            box = detections[0, 0, i, 3:7] * np.array([w, h, w, h])
                            self.last_detections.append((lbl, box.astype("int")))

                # Vẽ Box nhận diện từ AI
                for (lbl, (sX, sY, eX, eY)) in self.last_detections:
                    cv2.rectangle(frame, (sX, sY), (eX, eY), (0, 255, 0), 2)
                    cv2.putText(frame, lbl, (sX, sY-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
                    found.append(lbl)

                self.frame_ready.emit(frame, found)
            sock.close()
        except: pass

    def stop(self):
        self.running = False
        self.wait()

# --- GIAO DIỆN CHÍNH ---
class LidarWindowsApp(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.BASE_DIR = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
        uic.loadUi(os.path.join(self.BASE_DIR, "hienthi.ui"), self)
        
        self.PI_IP = '172.20.10.2'
        self.EXCEL_FILE = os.path.join(self.BASE_DIR, "data_log.xlsx")
        self.latest_frame_save = None
        self.last_val = 0
        self.l_sock = None
        self.lidar_buffer = ""

        # Nạp AI
        try:
            self.net = cv2.dnn.readNetFromCaffe(os.path.join(self.BASE_DIR, "MobileNetSSD_deploy.prototxt"), os.path.join(self.BASE_DIR, "MobileNetSSD_deploy.caffemodel"))
            self.ai_ready = True
        except: self.ai_ready = False

        self.cam_scene = QtWidgets.QGraphicsScene(self); self.CameraView.setScene(self.cam_scene)
        self.lidar_scene = QtWidgets.QGraphicsScene(self); self.lidarview.setScene(self.lidar_scene)
        self.lidarview.setBackgroundBrush(QtGui.QBrush(QColor(0, 0, 0)))

        self.t_l = QTimer(); self.t_l.timeout.connect(self.update_lidar)

        # Kết nối nút bấm
        self.pushButton.clicked.connect(self.start_app)   
        self.pushButton_2.clicked.connect(self.stop_app) 
        self.save_btn.clicked.connect(self.save_data_to_excel)
        self.export_btn.clicked.connect(self.start_export_thread)

    def start_app(self):
        self.stop_app() 
        self.cam_worker = CameraWorker(self.PI_IP, getattr(self, 'net', None), self.ai_ready)
        self.cam_worker.frame_ready.connect(self.display_camera)
        self.cam_worker.start()

        try:
            self.l_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.l_sock.connect((self.PI_IP, 5556))
            self.l_sock.setblocking(False)
            self.t_l.start(50)
        except: pass

    def stop_app(self):
        if hasattr(self, 'cam_worker'): self.cam_worker.stop()
        self.t_l.stop()
        if self.l_sock:
            try: self.l_sock.close()
            except: pass
            self.l_sock = None
        self.cam_scene.clear(); self.lidar_scene.clear()
        self.khoangcach.setText(""); self.vatcan.setText("TẠM DỪNG")

    def display_camera(self, frame, found):
        self.latest_frame_save = frame.copy()
        frame_disp = cv2.resize(frame, (580, 330))
        h, w = frame_disp.shape[:2]
        q_img = QImage(frame_disp.data, w, h, w*3, QImage.Format_RGB888).rgbSwapped()
        self.cam_scene.clear()
        self.cam_scene.addPixmap(QPixmap.fromImage(q_img))
        self.vatcan.setText(", ".join(list(set(found))) if found else "AN TOAN")

    def update_lidar(self):
        try:
            raw = self.l_sock.recv(4096).decode()
            self.lidar_buffer += raw
            if '\n' in self.lidar_buffer:
                lines = self.lidar_buffer.split('\n'); self.lidar_buffer = lines[-1]
                parts = lines[-2].strip().split(',')
                if len(parts) == 360:
                    data = [float(x) for x in parts]
                    v_pts = [data[a] for a in (list(range(355, 360)) + list(range(0, 6))) if data[a] > 150]
                    if v_pts:
                        cur = int(min(v_pts))
                        if abs(cur - self.last_val) > 8: self.khoangcach.setText(f"{cur} mm"); self.last_val = cur
                    self.draw_map(data)
        except: pass

    def draw_map(self, data):
        self.lidar_scene.clear(); self.lidar_scene.setSceneRect(-300, -300, 600, 600)
        p_grid = QPen(QColor(0, 80, 0)); f_font = QFont("Arial", 8, QFont.Bold)
        for r in range(1, 6):
            rad = r * 1000 * 0.05
            self.lidar_scene.addEllipse(-rad, -rad, rad*2, rad*2, p_grid)
            t = self.lidar_scene.addText(f"{r}m", f_font); t.setDefaultTextColor(Qt.white); t.setPos(rad, -15)
        self.lidar_scene.addLine(-300, 0, 300, 0, p_grid); self.lidar_scene.addLine(0, -300, 0, 300, p_grid)
        p_red = QPen(Qt.red, 3); p_green = QPen(Qt.green, 3)
        for a in range(360):
            dist = data[a]
            if dist > 150:
                rad_a = np.radians(a - 90)
                x = dist * 0.05 * np.cos(rad_a); y = dist * 0.05 * np.sin(rad_a)
                self.lidar_scene.addEllipse(x, y, 2, 2, p_red if dist < 1000 else p_green)

    def save_data_to_excel(self):
        try:
            now = QDateTime.currentDateTime().toString("HH:mm:ss dd-MM-yyyy")
            img_p = os.path.join(self.BASE_DIR, "temp_cam.jpg")
            if self.latest_frame_save is not None: cv2.imwrite(img_p, self.latest_frame_save)
            wb = load_workbook(self.EXCEL_FILE) if os.path.exists(self.EXCEL_FILE) else Workbook()
            ws = wb.active
            if ws.max_row == 1 and ws['A1'].value is None: ws.append(["Thời gian", "Vật cản", "Khoảng cách", "Hình ảnh"])
            row = ws.max_row + 1
            ws.cell(row=row, column=1, value=now); ws.cell(row=row, column=2, value=self.vatcan.text()); ws.cell(row=row, column=3, value=self.khoangcach.text())
            if os.path.exists(img_p):
                img = OpenpyxlImage(img_p); img.width, img.height = 140, 105; ws.add_image(img, f"D{row}")
            ws.row_dimensions[row].height = 85; wb.save(self.EXCEL_FILE)
            QtWidgets.QMessageBox.information(self, "Lưu", "Đã lưu vào Excel!")
        except Exception as e: QtWidgets.QMessageBox.critical(self, "Lỗi", str(e))

    def start_export_thread(self):
        self.export_btn.setEnabled(False); self.thread = QThread()
        self.worker = ExportWorker(self.EXCEL_FILE); self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.finished.connect(lambda m: (QtWidgets.QMessageBox.information(self, "Kết quả", m), self.export_btn.setEnabled(True)))
        self.thread.start()

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv); win = LidarWindowsApp(); win.show(); sys.exit(app.exec_())