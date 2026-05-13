#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <cmath>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h> 

#include "sl_lidar.h" 
#include "sl_lidar_driver.h"

using namespace sl;

bool ctrl_c_pressed = false;
void ctrlc(int) { ctrl_c_pressed = true; }

// Hàm vẽ vòng tròn rỗng cho thang đo
void drawCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius) {
    for (int w = 0; w < radius * 2; w++) {
        for (int h = 0; h < radius * 2; h++) {
            int dx = radius - w;
            int dy = radius - h;
            if ((dx*dx + dy*dy) <= (radius * radius) && (dx*dx + dy*dy) >= (radius-1)*(radius-1)) {
                SDL_RenderDrawPoint(renderer, centerX + dx, centerY + dy);
            }
        }
    }
}

int main(int argc, const char * argv[]) {
    const char * opt_com = "/dev/ttyUSB0";
    sl_u32 opt_baud = 460800;

    // Khởi tạo Lidar
    auto drv_res = createLidarDriver();
    ILidarDriver * drv = *drv_res;
    auto chan_res = createSerialPortChannel(opt_com, opt_baud);
    IChannel* channel = *chan_res;

    if (SL_IS_FAIL(drv->connect(channel))) {
        printf("Khong the ket noi Lidar!\n");
        return -1;
    }

    // Khởi tạo Đồ họa SDL2 và Font chữ
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) return -1;

    // Load font (Đường dẫn mặc định trên Raspberry Pi)
    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14);
    if (!font) printf("Canh bao: Khong tim thay font chu tai /usr/share/fonts/...\n");

    // Nhúng vào cửa sổ từ Python
    const char* win_id_str = getenv("SDL_WINDOWID");
    SDL_Window* window = (win_id_str) ? SDL_CreateWindowFrom((void*)(uintptr_t)atoll(win_id_str)) : 
                                        SDL_CreateWindow("Lidar View", 100, 100, 600, 600, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    drv->setMotorSpeed();
    drv->startScan(0, 1);
    signal(SIGINT, ctrlc);

    const int centerX = 300;
    const int centerY = 300;
    const float scale = 0.03f;  //slace = center/12000 mm, dùng để zoom in or zoom out

    while (!ctrl_c_pressed) {
        sl_lidar_response_measurement_node_hq_t nodes[8192];
        size_t count = 8192;

        if (SL_IS_OK(drv->grabScanDataHq(nodes, count))) {
            // Xóa màn hình màu đen
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            
            // --- 1. VẼ 12 VÒNG TRÒN THANG ĐO VÀ SỐ MÉT ---
            SDL_Color white = {255, 255, 255, 255}; 
            
            for (int r = 1; r <= 12; r++) {
                // Màu xanh định vị cho vòng tròn
                SDL_SetRenderDrawColor(renderer, 0, 80, 0, 255);
                int radius_px = (int)(r * 1000 * scale); // r mét = r*1000 mm
                drawCircle(renderer, centerX, centerY, radius_px);

                // Hiển thị số mét (màu trắng)
                if (font) {
                    char txt[10]; sprintf(txt, "%dm", r);
                    SDL_Surface* surf = TTF_RenderText_Solid(font, txt, white);
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                    // Đặt số mét dọc theo trục X
                    SDL_Rect rect = {centerX + radius_px + 2, centerY - 15, surf->w, surf->h};
                    if (rect.x < 595) SDL_RenderCopy(renderer, tex, NULL, &rect);
                    SDL_FreeSurface(surf);
                    SDL_DestroyTexture(tex);
                }
            }

            // Vẽ trục chữ thập chính giữa
            SDL_SetRenderDrawColor(renderer, 0, 120, 0, 255);
            SDL_RenderDrawLine(renderer, centerX, 0, centerX, 600);
            SDL_RenderDrawLine(renderer, 0, centerY, 600, centerY);

            // --- 2. VẼ DỮ LIỆU ĐIỂM QUÉT ---
            float min_dist_front = 1e6;

            for (size_t i = 0; i < count; i++) {
                float angle = nodes[i].angle_z_q14 * 90.f / (1 << 14);
                float dist = nodes[i].dist_mm_q2 / 4.0f;

                if (dist > 150) { // Bỏ qua vật cản quá gần cảm biến
                    // Tính khoảng cách phía trước (góc -5 đến +5 độ)
                    float norm_angle = (angle > 180) ? angle - 360 : angle;
                    if (abs(norm_angle) < 5.0f && dist < min_dist_front) min_dist_front = dist;

                    // Chuyển tọa độ cực sang tọa độ Oxy để vẽ
                    float rad = (angle - 90.0f) * M_PI / 180.0f;
                    int x = centerX + (int)(dist * scale * cos(rad));
                    int y = centerY + (int)(dist * scale * sin(rad));

                    // Điểm gần màu đỏ, xa màu xanh lá
                    if (dist < 1000) SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                    else SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

                    // Vẽ điểm đậm (3x3 pixel)
                    SDL_Rect p = {x - 1, y - 1, 3, 3};
                    SDL_RenderFillRect(renderer, &p);
                }
            }
            SDL_RenderPresent(renderer);

            // Gửi dữ liệu về Python qua stdout
            if (min_dist_front < 1e6) {
                printf("%.2f mm\n", min_dist_front);
                fflush(stdout);
            }
        }
        
        SDL_Event e;
        while(SDL_PollEvent(&e)) { if(e.type == SDL_QUIT) ctrl_c_pressed = true; }
    }

    // Giải phóng
    drv->stop(); drv->setMotorSpeed(0);
    delete drv; delete channel;
    if (font) TTF_CloseFont(font);
    TTF_Quit(); SDL_Quit();
    return 0;
}
