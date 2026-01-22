#include <raylib.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <map>
#include <deque>
#include <vector>
#include <string>
#include <chrono>

using json = nlohmann::json;
using namespace std::chrono_literals;

struct Player { std::string id; std::string name; int x; int y; int score; };
struct Sweet { std::string id; int x; int y; };

int main(int argc, char *argv[]) {
  // args
  std::string server = "ws://localhost:8080/ws";
  std::string name = "cpp-player";
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--server=", 0) == 0) server = a.substr(9);
    if (a.rfind("--name=", 0) == 0) name = a.substr(7);
    if (a == "--headless") headless = true;
  }

  ix::initNetSystem();
  ix::WebSocket ws;
  ws.setUrl(server);
  std::mutex mu;
  std::unordered_map<std::string, Player> players;
  std::unordered_map<std::string, Sweet> sweets;
  std::string selfID;
  std::atomic<bool> connected{false};

  // UI/State helpers
  std::deque<std::string> eventLog;
  std::atomic<bool> joined{false};
  std::string connStatus = "Disconnected";
  std::map<std::string, std::pair<float,float>> displayPos; // current displayed (possibly interpolated) positions
  std::map<std::string, std::pair<float,float>> prevPos;    // previous positions for interpolation
  std::map<std::string, std::pair<float,float>> velocity;   // cells per second
  std::chrono::steady_clock::time_point prevStateTime = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point lastStateTime = std::chrono::steady_clock::now();
  const std::chrono::milliseconds interpDuration = 120ms; // interpolation window
  const std::chrono::milliseconds maxExtrapolation = 300ms; // clamp extrapolation

  ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      std::cout << "WS open\n";
      connected = true;
      connStatus = "Connected";
    } else if (msg->type == ix::WebSocketMessageType::Message) {
      try {
        auto j = json::parse(msg->str);
        std::string t = j.value("type", "");
        if (t == "join_ack") {
          selfID = j.value("id", "");
          joined = true;
          connStatus = "Joined: " + selfID;
          std::cout << "join_ack id=" << selfID << std::endl;
        } else if (t == "state") {
          // update model and interpolation targets
          auto now = std::chrono::steady_clock::now();
          std::lock_guard<std::mutex> lk(mu);
          // compute dt in seconds since previous state
          float dt = std::max(1e-3f, std::chrono::duration<float>(now - prevStateTime).count());

          // record previous positions
          for (auto &kv : players) {
            prevPos[kv.first] = displayPos.count(kv.first) ? displayPos[kv.first] : std::make_pair((float)kv.second.x, (float)kv.second.y);
          }
          players.clear(); sweets.clear();
          for (auto &p : j["players"]) {
            Player pl{p.value("id", ""), p.value("name", ""), p.value("x", 0), p.value("y", 0), p.value("score", 0)};
            players[pl.id] = pl;
            auto tgt = std::make_pair((float)pl.x, (float)pl.y);
            // previous position for this player
            std::pair<float,float> prev = prevPos.count(pl.id) ? prevPos[pl.id] : tgt;
            // velocity in cells per second
            velocity[pl.id] = std::make_pair((tgt.first - prev.first)/dt, (tgt.second - prev.second)/dt);
            // set display target
            displayPos[pl.id] = tgt;
            if (!prevPos.count(pl.id)) prevPos[pl.id] = prev;
          }
          for (auto &s : j["sweets"]) {
            Sweet sw{s.value("id", ""), s.value("x", 0), s.value("y", 0)};
            sweets[sw.id] = sw;
          }
          prevStateTime = now;
          lastStateTime = now;
        } else if (t == "event") {
          std::string ev = j.dump();
          // push to log, keep small buffer
          eventLog.push_front(ev);
          if (eventLog.size() > 6) eventLog.pop_back();
          std::cout << "event: " << ev << std::endl;
        } else if (t == "game_over") {
          std::cout << "GAME OVER!" << std::endl;
          connStatus = "GAME OVER";
          // Add variable "isGameOver" to indicate game over state and release display 
        } else {
          std::cout << "msg: " << j.dump() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "json parse error: " << e.what() << " raw=" << msg->str << std::endl;
      }
    } else if (msg->type == ix::WebSocketMessageType::Close) {
      std::cout << "WS closed\n";
      connected = false;
      connStatus = "Disconnected";
      joined = false;
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      std::cerr << "WS error: " << msg->errorInfo.reason << std::endl;
      connected = false;
      connStatus = "Error";
      joined = false;
    }
  });

  // enable automatic reconnection (useful for transient network issues)
  ws.enableAutomaticReconnection();
  ws.setMinWaitBetweenReconnectionRetries(1);
  ws.setMaxWaitBetweenReconnectionRetries(5);
  ws.start();
  connStatus = "Connecting...";

  // wait until connection open before sending join (with timeout)
  auto openDeadline = std::chrono::steady_clock::now() + 1000ms;
  while (!connected && std::chrono::steady_clock::now() < openDeadline) {
    std::this_thread::sleep_for(20ms);
  }
  if (!connected) {
    std::cerr << "warning: websocket did not open in time" << std::endl;
  }

  // send join
  {
    json j = { {"type", "join"}, {"name", name} };
    ws.sendText(j.dump());
  }

  if (headless) {
    // simple scripted moves, print events
    std::cout << "Running headless client to " << server << " as " << name << std::endl;
    // wait for join ack (selfID) or timeout
    auto waitDeadline = std::chrono::steady_clock::now() + 1000ms;
    while (selfID.empty() && std::chrono::steady_clock::now() < waitDeadline) {
      std::this_thread::sleep_for(50ms);
    }
    if (selfID.empty()) std::cout << "warning: did not receive join_ack, proceeding anyway" << std::endl;

    // run for a short while, send a move every 200ms
    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(200ms);
      json mv = {{"type", "move"}, {"dir", (i%2==0?"right":"left")}};
      ws.sendText(mv.dump());
    }
    // wait a bit to see events
    std::this_thread::sleep_for(500ms);
    ws.stop();
    ix::uninitNetSystem();
    return 0;
  }

  // UI mode
  const int width = 640, height = 480;
  InitWindow(width, height, "sr-client");
  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    // input -> send moves
    if (IsKeyPressed(KEY_UP)) { json mv = {{"type", "move"}, {"dir", "up"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_DOWN)) { json mv = {{"type", "move"}, {"dir", "down"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_LEFT)) { json mv = {{"type", "move"}, {"dir", "left"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_RIGHT)) { json mv = {{"type", "move"}, {"dir", "right"}}; ws.sendText(mv.dump()); }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    std::lock_guard<std::mutex> lk(mu);
    // draw a simple grid based on 10x10 world (server default)
    int cols = 10, rows = 10;
    int cellW = width / cols, cellH = height / rows;
    // draw grid
    for (int y = 0; y <= rows; ++y) DrawLine(0, y*cellH, width, y*cellH, LIGHTGRAY);
    for (int x = 0; x <= cols; ++x) DrawLine(x*cellW, 0, x*cellW, height, LIGHTGRAY);

    // draw sweets
    for (auto &kv : sweets) {
      auto &s = kv.second;
      float fx = s.x, fy = s.y;
      // interpolation not necessary for sweets (static between states)
      int cx = (int)(fx*cellW + cellW/2);
      int cy = (int)(fy*cellH + cellH/2);
      DrawCircle(cx, cy, std::min(cellW, cellH)/4, RED);
    }

    // compute interpolation / extrapolation factors
    auto now = std::chrono::steady_clock::now();
    float elapsedMs = std::chrono::duration<float, std::milli>(now - lastStateTime).count();
    float t = std::min(1.0f, elapsedMs / (float)interpDuration.count());
    bool doExtrap = elapsedMs > interpDuration.count();
    float extraSec = 0.0f;
    if (doExtrap) {
      extraSec = std::min((elapsedMs - (float)interpDuration.count())/1000.0f, std::chrono::duration<float>(maxExtrapolation).count());
    }

    // draw players (with interpolation/extrapolation)
    for (auto &kv : players) {
      auto &p = kv.second;
      float px = (float)p.x, py = (float)p.y;
      auto itPrev = prevPos.find(p.id);
      std::pair<float,float> from = itPrev != prevPos.end() ? itPrev->second : std::make_pair(px, py);
      auto itTgt = displayPos.find(p.id);
      std::pair<float,float> to = itTgt != displayPos.end() ? itTgt->second : std::make_pair(px, py);
      float ix, iy;
      if (!doExtrap) {
        ix = from.first + (to.first - from.first) * t;
        iy = from.second + (to.second - from.second) * t;
      } else {
        auto itVel = velocity.find(p.id);
        if (itVel != velocity.end()) {
          ix = to.first + itVel->second.first * extraSec;
          iy = to.second + itVel->second.second * extraSec;
        } else {
          ix = to.first;
          iy = to.second;
        }
      }
      // clamp to grid
      ix = std::max(0.0f, std::min((float)(cols-1), ix));
      iy = std::max(0.0f, std::min((float)(rows-1), iy));
      int cx = (int)(ix*cellW + cellW/2);
      int cy = (int)(iy*cellH + cellH/2);
      int radius = std::min(cellW, cellH)/3;
      // highlight local player
      if (p.id == selfID) {
        DrawCircle(cx, cy, radius + 6, Fade(GOLD, 0.6f));
      }
      Color col = (p.id == selfID) ? BLUE : DARKPURPLE;
      DrawCircle(cx, cy, radius, col);
      DrawText(p.name.c_str(), cx - cellW/3, cy - cellH/2, 12, BLACK);
    }

    // HUD: connection status (top-left)
    DrawText(connStatus.c_str(), 10, 6, 14, DARKGRAY);
    if (joined && !selfID.empty()) {
      std::string lab = "id: " + selfID;
      DrawText(lab.c_str(), 10, 26, 12, DARKGRAY);
    }

    // Reconnect button (top-right)
    Rectangle btn = { (float)(width - 110), 6, 100, 28 };
    bool hover = CheckCollisionPointRec(GetMousePosition(), btn);
    Color btnColor = hover ? LIGHTGRAY : Fade(LIGHTGRAY, 0.5f);
    DrawRectangleRec(btn, btnColor);
    DrawText("Reconnect", (int)(btn.x + 10), (int)(btn.y + 6), 12, DARKGRAY);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hover) {
      // trigger reconnect
      ws.stop();
      std::this_thread::sleep_for(50ms);
      ws.start();
      connStatus = "Reconnecting...";
      joined = false;
    }

    // HUD: scores (top-right)
    int sx = width - 150, sy = 44;
    DrawRectangle(sx-6, sy-6, 150, 120, Fade(LIGHTGRAY, 0.1f));
    DrawText("Scores:", sx, sy, 12, DARKGRAY);
    int line = 1;
    for (auto &kv : players) {
      std::string t = kv.second.name + ": " + std::to_string(kv.second.score);
      DrawText(t.c_str(), sx, sy + 14*line, 12, DARKGRAY);
      line++;
      if (line > 6) break;
    }

    // Event log (bottom-left)
    int logY = height - 16;
    int idx = 0;
    for (auto &e : eventLog) {
      DrawText(e.c_str(), 10, logY - idx*14, 10, DARKGRAY);
      idx++;
      if (idx >= 6) break;
    }
    if (connStatus == "GAME OVER") {
        DrawRectangle(0, 0, width, height, Fade(BLACK, 0.7f));
        DrawText("GAME OVER", width/2 - 100, height/2 - 20, 40, RED);
        
        // Display win/lose based on scores
        if (!selfID.empty() && players.count(selfID)) {
            auto &selfPlayer = players[selfID];
            bool isWinner = true;
            for (auto &kv : players) {
                if (kv.first != selfID && kv.second.score >= selfPlayer.score) {
                    isWinner = false;
                    break;
                }
            }
            if (isWinner) {
                DrawText("YOU WIN!", width/2 - 80, height/2 + 40, 30, GREEN);
            } else {
                DrawText("YOU LOSE!", width/2 - 80, height/2 + 40, 30, RED);
            }
        }
    }

    EndDrawing();
  }

  CloseWindow();
  ws.stop();
  ix::uninitNetSystem();
  return 0;
}
