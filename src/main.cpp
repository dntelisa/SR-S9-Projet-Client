#include <raylib.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <nlohmann/json.hpp>

#include <iostream> 
#include <thread>
#include <atomic> // for simple variable thread safety
#include <mutex>
#include <unordered_map>
#include <map>
#include <deque>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <cstdlib> 
#include <ctime>   

// translate nlohmann::json to json for brevity
using json = nlohmann::json;
using namespace std::chrono_literals;

// Mirror of server-side structs
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
  ws.setUrl(server); // WebSocket server URL
  std::mutex mu; // Protecting access to the model is useful because we are simultaneously manipulating the reader for display and the network.
  std::unordered_map<std::string, Player> players; // id -> Player, map faster for lookups
  std::unordered_map<std::string, Sweet> sweets;
  std::string selfID;
  std::atomic<bool> connected{false}; // connection state thread-safe

  // UI/State helpers
  std::deque<std::string> eventLog; 
  std::atomic<bool> joined{false};
  std::string connStatus = "Disconnected";
  std::chrono::steady_clock::time_point gameOverTime;

  // interpolation state
  std::map<std::string, std::pair<float,float>> displayPos; // current displayed (possibly interpolated) positions
  std::map<std::string, std::pair<float,float>> prevPos;    // previous positions for interpolation
  std::map<std::string, std::pair<float,float>> velocity;   // cells per second
  std::chrono::steady_clock::time_point prevStateTime = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point lastStateTime = std::chrono::steady_clock::now();
  const std::chrono::milliseconds interpDuration = 120ms; // interpolation window
  const std::chrono::milliseconds maxExtrapolation = 50ms; // clamp extrapolation

  // WebSocket message handler, runs in its own thread, wakes up on incoming messages
  // [&] permits access to external variables by reference
  ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
    // handle message based on type
    if (msg->type == ix::WebSocketMessageType::Open) {
      // If connection is open, send join message
      std::cout << "WS open\n";
      connected = true;
      connStatus = "Connected";
      // Send join request
      json j = { {"type", "join"}, {"name", name} };
      ws.sendText(j.dump());
    } else if (msg->type == ix::WebSocketMessageType::Message) {
      try {
        auto j = json::parse(msg->str);
        std::string t = j.value("type", "");
        // If server sent a join_ack, record our player ID
        if (t == "join_ack") {
          selfID = j.value("id", "");
          joined = true;
          connStatus = "Joined: " + selfID;
          std::cout << "join_ack id=" << selfID << std::endl;
        } else if (t == "state") {
          // update model and interpolation targets
          auto now = std::chrono::steady_clock::now();
          std::lock_guard<std::mutex> lk(mu); // lock model during update to avoid problem when the UI thread reads it to draw game
          // compute dt in seconds since previous state
          float dt = std::max(1e-3f, std::chrono::duration<float>(now - prevStateTime).count());

          // record previous positions -> interpolation
          for (auto &kv : players) {
            prevPos[kv.first] = displayPos.count(kv.first) ? displayPos[kv.first] : std::make_pair((float)kv.second.x, (float)kv.second.y);
          }
          players.clear(); sweets.clear(); // clean all and refill, it for the case of a player has left
          for (auto &p : j["players"]) {
            // Recreate player
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
          if (connStatus == "GAME OVER") {
            // Check how long since game over
             auto now = std::chrono::steady_clock::now();
             auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - gameOverTime).count();
             
             // If it's been less than 4 seconds, we IGNORE this state (we remain visually in Game Over)
             // The server pauses for 5 seconds, so 4 seconds is a good safety margin.
             if (elapsed < 4) {
                 return; // We exit the function; we do not update the display.
             }

             // Otherwise let's start again
             connStatus = "Joined: " + selfID;
          }

        } else if (t == "event") {
          std::string ev = j.dump();
          // push to log, keep small buffer
          eventLog.push_front(ev);
          if (eventLog.size() > 6) eventLog.pop_back();
          std::cout << "event: " << ev << std::endl;
        } else if (t == "game_over") {
          std::cout << "GAME OVER!" << std::endl;
          connStatus = "GAME OVER";
          gameOverTime = std::chrono::steady_clock::now();
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
  ws.setMinWaitBetweenReconnectionRetries(1); // if connection drops, retry in 1 second
  ws.setMaxWaitBetweenReconnectionRetries(5); // if connection drops, retry in 5 second
  ws.start(); // start connection, launches network thread in background
  connStatus = "Connecting...";

  // wait until connection open before sending join (with timeout)
  auto openDeadline = std::chrono::steady_clock::now() + 1000ms;
  while (!connected && std::chrono::steady_clock::now() < openDeadline) {
    std::this_thread::sleep_for(20ms); // ws.start() is instantaneous, but the actual connection takes time (a few milliseconds). We put the main thread into sleep ("Sleep") until connected becomes true (thanks to the callback) OR until we have waited more than a second (Timeout).
  }
  if (!connected) {
    std::cerr << "warning: websocket did not open in time" << std::endl;
  }

  // Headless mode: simple bot that makes random moves
  if (headless) {
    std::cout << "Running headless client to " << server << " as " << name << std::endl;
    
    // Wait ack_join
    auto waitDeadline = std::chrono::steady_clock::now() + 1000ms;
    while (selfID.empty() && std::chrono::steady_clock::now() < waitDeadline) {
      std::this_thread::sleep_for(50ms);
    }
    
    // Initialize randomness with a unique seed per bot (based on time + name)
    // This ensures different bots have different movement patterns
    std::srand(std::time(nullptr) + std::hash<std::string>{}(name));

    // List of possible directions
    const std::string dirs[] = {"up", "down", "left", "right"};

    // Infinite loop of random moves
    while (true) {
      // Random pauses between 200ms and 500ms to vary the speeds
      int sleepMs = 200 + (std::rand() % 300);
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

      // Choose a direction at random (0 to 3)
      int r = std::rand() % 4;
      std::string dir = dirs[r];

      json mv = {{"type", "move"}, {"dir", dir}};
      ws.sendText(mv.dump());
    }
    
    return 0;
  }

  // UI mode
  const int width = 640, height = 480;
  InitWindow(width, height, "sr-client");
  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    // input -> if key pressed, prepare json message and send move command
    if (IsKeyPressed(KEY_UP)) { json mv = {{"type", "move"}, {"dir", "up"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_DOWN)) { json mv = {{"type", "move"}, {"dir", "down"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_LEFT)) { json mv = {{"type", "move"}, {"dir", "left"}}; ws.sendText(mv.dump()); }
    if (IsKeyPressed(KEY_RIGHT)) { json mv = {{"type", "move"}, {"dir", "right"}}; ws.sendText(mv.dump()); }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    std::lock_guard<std::mutex> lk(mu); // lock model during read to avoid problem when the network thread updates it
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

    // compute elapsed time since last state update
    float elapsedMs = std::chrono::duration<float, std::milli>(now - lastStateTime).count();
    float t = std::min(1.0f, elapsedMs / (float)interpDuration.count());
    float extraSec = 0.0f;

    // draw players (with interpolation/extrapolation)
    for (auto &kv : players) {
      auto &p = kv.second;
      float px = (float)p.x, py = (float)p.y;
      auto itPrev = prevPos.find(p.id);
      std::pair<float,float> from = itPrev != prevPos.end() ? itPrev->second : std::make_pair(px, py);
      auto itTgt = displayPos.find(p.id);
      std::pair<float,float> to = itTgt != displayPos.end() ? itTgt->second : std::make_pair(px, py);
      float ix, iy;
      // compute intermediated position
      ix = from.first + (to.first - from.first) * t;
      iy = from.second + (to.second - from.second) * t;      // clamp to grid
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

    // Copy players to a temporary vector for sorting
    std::vector<Player> sortedPlayers;
    for (auto &kv : players) {
        sortedPlayers.push_back(kv.second);
    }

    // Order by decreasing score, with ID as tie-breaker
    std::sort(sortedPlayers.begin(), sortedPlayers.end(), [](const Player& a, const Player& b) {
        if (a.score != b.score) {
            return a.score > b.score; 
        }
        return a.id < b.id; 
    });

    // Display top scores
    for (const auto &p : sortedPlayers) {
      std::string t = p.name + ": " + std::to_string(p.score);
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
                // Display the winner's name
                std::string winnerName;
                int highestScore = -1;
                for (auto &kv : players) {
                    if (kv.second.score > highestScore) {
                        highestScore = kv.second.score;
                        winnerName = kv.second.name;
                    }
                }
                std::string winnerText = "The winner is: " + winnerName;
                DrawText(winnerText.c_str(), width/2 - 130, height/2 + 80, 20, LIGHTGRAY);
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
