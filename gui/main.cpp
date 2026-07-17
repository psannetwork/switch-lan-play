#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <array>
#include <memory>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose

std::string get_executable_dir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}
#else
#include <cstdio>
#define POPEN popen
#define PCLOSE pclose
std::string get_executable_dir() {
    // Linux/MSYS2での簡易実装（カレントディレクトリをベースにする）
    return ".";
}
#endif

// プロセス管理用のグローバル変数
#ifdef _WIN32
HANDLE process_handle = nullptr;
#else
FILE* pipe_handle = nullptr;
#endif
std::vector<std::string> log_buffer;

void run_process(const std::string& cmd, bool absolute_cmd = false) {
#ifdef _WIN32
    if (process_handle) return;
    
    std::string base_dir = get_executable_dir();
    std::string full_cmd = absolute_cmd ? cmd : (base_dir + "\\" + cmd);
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, (LPSTR)full_cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        process_handle = pi.hProcess;
        CloseHandle(pi.hThread);
        log_buffer.push_back("Started: " + full_cmd);
    }
#else
    if (pipe_handle) return; 
    
    std::string base_dir = get_executable_dir();
    std::string full_cmd = absolute_cmd ? cmd : (base_dir + "/" + cmd);
    
    pipe_handle = POPEN(full_cmd.c_str(), "r");
    log_buffer.push_back("Started: " + full_cmd);
#endif
}

void stop_process() {
#ifdef _WIN32
    if (process_handle) {
        TerminateProcess(process_handle, 0);
        CloseHandle(process_handle);
        process_handle = nullptr;
        log_buffer.push_back("Process stopped.");
    }
#else
    if (pipe_handle) {
        PCLOSE(pipe_handle);
        pipe_handle = nullptr;
        log_buffer.push_back("Process stopped.");
    }
#endif
}

enum Language { LANG_EN, LANG_JA };
Language current_lang = LANG_EN;

void save_settings(Language lang, int s_proto, int c_proto) {
    std::ofstream ofs(get_executable_dir() + "/settings.txt");
    ofs << "lang=" << lang << "\n";
    ofs << "s_proto=" << s_proto << "\n";
    ofs << "c_proto=" << c_proto << "\n";
}

void load_settings(Language &lang, int &s_proto, int &c_proto) {
    std::ifstream ifs(get_executable_dir() + "/settings.txt");
    std::string line;
    while (std::getline(ifs, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (key == "lang") lang = (Language)std::stoi(val);
            else if (key == "s_proto") s_proto = std::stoi(val);
            else if (key == "c_proto") c_proto = std::stoi(val);
        }
    }
}

// ローカライズされた文字列を取得する関数
const char* L(const char* en, const char* ja) {
    return (current_lang == LANG_JA) ? ja : en;
}

// --- Server Management ---
struct ServerEntry {
    std::string name;
    std::string addr;
};
std::vector<ServerEntry> server_list;
void load_servers() {
    server_list.clear();
    std::ifstream ifs(get_executable_dir() + "/servers.txt");
    std::string line;
    while (std::getline(ifs, line)) {
        size_t pos = line.find(',');
        if (pos != std::string::npos) {
            server_list.push_back({line.substr(0, pos), line.substr(pos + 1)});
        }
    }
}
void save_servers() {
    std::ofstream ofs(get_executable_dir() + "/servers.txt");
    for (const auto& s : server_list) ofs << s.name << "," << s.addr << "\n";
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;

    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // ウィンドウ作成（サイズを少し小さめに）
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Switch LAN Play UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 日本語フォントをロード (Windows環境を想定してNoto Sans CJKなどを試す。フォントパスは環境依存)
    // Windowsでは通常 C:\Windows\Fonts にあります
    ImFontConfig font_config;
    font_config.OversampleH = 1;
    font_config.OversampleV = 1;
    font_config.PixelSnapH = 1;
    
    // Noto Sans CJK JP を試みる（MSYS2環境のパスまたはWindowsフォントパス）
    // とりあえず、システム上のフォントを見つける方法が簡易的だが、ここを修正
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x3000, 0x30FF, 0x4E00, 0x9FAF, 0 };
    // フォントパスは絶対パスで指定するか、ランタイムで探す必要がある
    // ここでは単純に日本語対応フォントをロードしようとする
#ifdef LANPLAY_LINUX
    io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18.0f, &font_config, ranges);
#else
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 18.0f, &font_config, ranges);
#endif
    io.FontGlobalScale = 1.0f; // サイズを調整
bool done = false;
static char port[10] = "11451";
static char monitorPort[10] = "11452"; // 追加
static char nick[64] = "Player";
static char server_password[64] = "";
static char client_password[64] = "";
static int server_protocol = 0;
static int client_protocol = 0;

// 設定読み込み
load_settings(current_lang, server_protocol, client_protocol);

while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) done = true;
    }
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem(L("Server", "サーバー"))) {
                ImGui::Dummy(ImVec2(0, 10)); // 上部に余白
                ImGui::InputText(L("Port", "ポート"), port, IM_ARRAYSIZE(port));
                if (server_protocol == 1) {
                    ImGui::InputText(L("Monitor Port", "監視ポート"), monitorPort, IM_ARRAYSIZE(monitorPort));
                }
                if (ImGui::RadioButton("UDP##server", &server_protocol, 0)) save_settings(current_lang, server_protocol, client_protocol);
                ImGui::SameLine();
                if (ImGui::RadioButton("TCP##server", &server_protocol, 1)) save_settings(current_lang, server_protocol, client_protocol);
                ImGui::InputText(L("Password", "パスワード"), server_password, IM_ARRAYSIZE(server_password), ImGuiInputTextFlags_Password);
                
                ImGui::Dummy(ImVec2(0, 5));
                if (ImGui::Button(L("Start Server", "サーバー開始"), ImVec2(150, 40))) {
                    // Windowsではnodeを使ってts-nodeを読み込むのが確実
#ifdef LANPLAY_LINUX
                    std::string cmd = "node server/node_modules/ts-node/dist/bin.js server/src/main.ts --port " + std::string(port);
#else
                    std::string cmd = "node server\\node_modules\\ts-node\\dist\\bin.js server\\src\\main.ts --port " + std::string(port);
#endif
                    cmd += " --monitorPort " + std::string(monitorPort); // 常時追加
                    if (server_protocol == 1) {
                        cmd += " --protocol tcp";
                    } else cmd += " --protocol udp";
                    if (strlen(server_password) > 0) cmd += " --password " + std::string(server_password);
                    run_process(cmd, true);
                }
                ImGui::EndTabItem();
            }
// ...

// ... in main ...
    load_servers();
    static char new_server_name[32] = "";
    static char new_server_addr[64] = "";
    static int selected_server = -1;

// ... in Client Tab ...
            if (ImGui::BeginTabItem(L("Client", "クライアント"))) {
                ImGui::Dummy(ImVec2(0, 10));
                
                if (ImGui::BeginListBox(L("Saved Servers", "保存済みサーバー"), ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing()))) {
                    for (int i = 0; i < server_list.size(); i++) {
                        if (ImGui::Selectable((server_list[i].name + " (" + server_list[i].addr + ")").c_str(), selected_server == i))
                            selected_server = i;
                    }
                    ImGui::EndListBox();
                }
                if (ImGui::Button(L("Delete", "削除")) && selected_server != -1) {
                    server_list.erase(server_list.begin() + selected_server);
                    selected_server = -1;
                    save_servers();
                }
                ImGui::Separator();
                ImGui::InputText(L("Name", "名前"), new_server_name, IM_ARRAYSIZE(new_server_name));
                ImGui::InputText(L("Address", "アドレス"), new_server_addr, IM_ARRAYSIZE(new_server_addr));
                if (ImGui::Button(L("Add Server", "サーバー追加"))) {
                    if (strlen(new_server_name) > 0 && strlen(new_server_addr) > 0) {
                        server_list.push_back({new_server_name, new_server_addr});
                        save_servers();
                    }
                }
                ImGui::Separator();
                
                ImGui::InputText(L("Username", "ユーザー名"), nick, IM_ARRAYSIZE(nick));
                ImGui::InputText(L("Password", "パスワード"), client_password, IM_ARRAYSIZE(client_password), ImGuiInputTextFlags_Password);
                
                if (ImGui::Button(L("Start Client", "クライアント開始"), ImVec2(200, 40)) && selected_server != -1) {
                    std::string cmd = "bin\\lan-play.exe --relay-server-addr " + server_list[selected_server].addr;
                    // Auto-detection assumed by removing hardcoded flags if possible, 
                    // or keeping default to udp and letting client negotiate.
                    cmd += " --udp";
                    if (strlen(nick) > 0) cmd += " --username " + std::string(nick);
                    if (strlen(client_password) > 0) cmd += " --password " + std::string(client_password);
                    run_process(cmd);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("How to setup switch", "Switchのセットアップ方法"))) {
                ImGui::BeginChild("SetupInstructions", ImVec2(0, 0), true);
                
                ImGui::TextWrapped(L("You need to manually edit the configuration of your local network with this informations.", "ローカルネットワークの設定を手動で変更する必要があります。"));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 5));

                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), L("Note:", "注意:"));
                ImGui::TextWrapped(L("The IP address can be any from 10.13.0.1 to 10.13.255.254, excepting 10.13.37.1. But don't use the same IP address with your friend.", "IPアドレスは10.13.0.1から10.13.255.254の間であれば何でも構いません（10.13.37.1を除く）。ただし、フレンドと同じIPアドレスは使用しないでください。"));
                ImGui::Dummy(ImVec2(0, 10));

                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), L("IP Address Settings", "IPアドレス設定"));
                ImGui::Text(L("IP Address:   10.13.XX.YY", "IPアドレス:   10.13.XX.YY"));
                ImGui::Text(L("Subnet Mask:  255.255.0.0", "サブネットマスク:  255.255.0.0"));
                ImGui::Text(L("Gateway:      10.13.37.1", "ゲートウェイ:      10.13.37.1"));
                ImGui::Dummy(ImVec2(0, 10));

                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), L("DNS settings (these are the 90DNS servers)", "DNS設定 (これらは90DNSサーバーです)"));
                ImGui::Text(L("Primary DNS:   163.172.141.219", "優先DNS:   163.172.141.219"));
                ImGui::Text(L("Secondary DNS: 207.246.121.77", "代替DNS: 207.246.121.77"));

                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Dummy(ImVec2(0, 20));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));
        
        // --- Display Latency and Server Info ---
        static std::string latency_str = "N/A";
        static std::string info_str = "N/A";
        static uint64_t last_check = 0;
        if (SDL_GetTicks() - last_check > 1000) { // Update every 1s
            std::ifstream ifs("latency.txt");
            if (ifs.is_open()) {
                std::getline(ifs, latency_str);
                latency_str += " ms";
            } else {
                latency_str = "N/A";
            }

            std::ifstream ifs_info("server_info.txt");
            if (ifs_info.is_open()) {
                std::getline(ifs_info, info_str);
            } else {
                info_str = "N/A";
            }
            
            last_check = SDL_GetTicks();
        }
        ImGui::Text("Latency: %s", latency_str.c_str());
        ImGui::Text("Server Info: %s", info_str.c_str());

        if (ImGui::Button("Stop Process", ImVec2(120, 30))) stop_process();

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Logs:");
        
        // ログ領域のサイズを残り全体にする
        ImGui::BeginChild("LogRegion", ImVec2(0, -10), true);
        for (const auto& line : log_buffer) ImGui::TextUnformatted(line.c_str());
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    stop_process();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
