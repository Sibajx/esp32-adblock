// Implementación del simulador del ESP32 sobre Linux.
#include <arpa/inet.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>

#include <atomic>
#include <map>
#include <mutex>

#include "AsyncUDP.h"
#include "HTTPClient.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "WebServer.h"
#include "WiFi.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "lwip/etharp.h"

namespace stdfs = std::filesystem;
extern char** environ;

static int envInt(const char* name, int def) {
  const char* v = getenv(name);
  return v ? atoi(v) : def;
}

// ---------- Básicos ----------

static std::string flashDir() {
  const char* r = getenv("SIM_FLASH");
  return r && *r ? r : "./sim-flash";
}

// Reloj: tiempo real + lo que diga $SIM_FLASH/.clock_ms (se relee como mucho cada 200 ms reales).
// Así las pruebas adelantan el reloj sin esperar (ventana de 30 min, portal, botón).
static const auto kStart = std::chrono::steady_clock::now();
static std::atomic<int64_t> gClockOffset{0};
static std::atomic<int64_t> gClockReadAt{-1000};
static std::mutex gClockMutex;
uint32_t millis() {
  int64_t real = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - kStart).count();
  if (real - gClockReadAt.load() >= 200) {
    std::unique_lock<std::mutex> l(gClockMutex, std::try_to_lock);
    if (l.owns_lock() && real - gClockReadAt.load() >= 200) {
      int64_t off = 0;
      if (FILE* f = fopen((flashDir() + "/.clock_ms").c_str(), "r")) {
        long long v = 0;
        if (fscanf(f, "%lld", &v) == 1) off = v;
        fclose(f);
      }
      gClockOffset = off;
      gClockReadAt = real;
    }
  }
  return uint32_t(uint64_t(real + gClockOffset.load()));
}
void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---------- GPIO: botón BOOT y LED ----------
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
  if (pin != 0) return HIGH;
  struct stat st;
  return stat((flashDir() + "/.boot").c_str(), &st) == 0 ? LOW : HIGH;
}

SerialPort Serial;
static std::mutex gSerialMutex;

void digitalWrite(uint8_t pin, uint8_t val) {
  static int last[40];
  static bool init = false;
  if (!init) {
    for (int& v : last) v = -1;
    init = true;
  }
  if (pin >= 40 || last[pin] == int(val ? 1 : 0)) return;
  last[pin] = val ? 1 : 0;
  if (pin == 2) Serial.printf("[sim] led %d\n", last[pin]);
}

// ---------- Tabla ARP de mentira (ver lwip/etharp.h) ----------
static struct netif gSimNetif;
struct netif* netif_default = &gSimNetif;

ssize_t etharp_find_addr(struct netif*, const ip4_addr_t* ipaddr, struct eth_addr** eth_ret, const ip4_addr_t** ip_ret) {
  static thread_local eth_addr mac;
  static thread_local ip4_addr_t ip;
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&ipaddr->addr);
  if (b[0] != 127 || b[1] != 0 || b[2] != 0) return -1;
  uint8_t def[6] = {0x02, 0, 0, 0, 0, b[3]};
  memcpy(mac.addr, def, 6);
  char want[20];
  snprintf(want, sizeof want, "127.0.0.%u", unsigned(b[3]));
  if (FILE* f = fopen((flashDir() + "/.arp").c_str(), "r")) {
    char line[128];
    while (fgets(line, sizeof line, f)) {
      char who[32] = {0}, what[32] = {0};
      if (sscanf(line, "%31s %31s", who, what) != 2 || strcmp(who, want) != 0) continue;
      unsigned m[6];
      if (strcmp(what, "-") == 0) {
        fclose(f);
        return -1;
      }
      if (sscanf(what, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6)
        for (int i = 0; i < 6; i++) mac.addr[i] = uint8_t(m[i]);
    }
    fclose(f);
  }
  ip = *ipaddr;
  *eth_ret = &mac;
  if (ip_ret) *ip_ret = &ip;
  return 0;
}

// ---------- Serie ----------
int SerialPort::printf(const char* fmt, ...) {
  std::lock_guard<std::mutex> l(gSerialMutex);
  va_list ap;
  va_start(ap, fmt);
  int n = vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
  return n;
}
void SerialPort::print(const char* s) {
  std::lock_guard<std::mutex> l(gSerialMutex);
  fputs(s, stdout);
  fflush(stdout);
}
void SerialPort::print(char c) {
  std::lock_guard<std::mutex> l(gSerialMutex);
  fputc(c, stdout);
  fflush(stdout);
}
void SerialPort::println(const char* s) {
  std::lock_guard<std::mutex> l(gSerialMutex);
  puts(s);
  fflush(stdout);
}
size_t SerialPort::write(const uint8_t* buf, size_t n) {
  std::lock_guard<std::mutex> l(gSerialMutex);
  size_t w = fwrite(buf, 1, n, stdout);
  fflush(stdout);
  return w;
}

static std::deque<uint8_t> gSerialIn;
static void pumpStdin() {
  static int enabled = -1;
  if (enabled < 0) {
    const char* e = getenv("SIM_SERIAL_STDIN");
    enabled = e && strcmp(e, "1") == 0;
    if (enabled) fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
  }
  if (!enabled) return;
  uint8_t buf[512];
  ssize_t n = ::read(0, buf, sizeof buf);
  if (n > 0) gSerialIn.insert(gSerialIn.end(), buf, buf + n);
}
int SerialPort::available() {
  pumpStdin();
  return int(gSerialIn.size());
}
int SerialPort::read() {
  if (gSerialIn.empty()) pumpStdin();
  if (gSerialIn.empty()) return -1;
  int c = gSerialIn.front();
  gSerialIn.pop_front();
  return c;
}

// Heap "del ESP32": 320 KB menos lo que el firmware ha pedido desde que arrancó (aprox.).
static size_t gHeapBase = mallinfo2().uordblks;
size_t simHeapInUse() {
  size_t now = mallinfo2().uordblks;
  return now > gHeapBase ? now - gHeapBase : 0;
}
EspClass ESP;
uint32_t EspClass::getFreeHeap() {
  // Lo que el firmware real tiene libre tras WiFi + pila TCP/IP: ~250 KB.
  long free = 250 * 1024 - long(simHeapInUse());
  return free > 0 ? uint32_t(free) : 0;
}
void EspClass::restart() {
  Serial.println("[sim] ESP.restart()");
  // Como la placa real: sin destructores ni atexit. exit() con los hilos del DNS aún vivos
  // a veces se colgaba (la placa quedaba con los puertos tomados y la prueba siguiente fallaba).
  fflush(nullptr);
  _exit(3);
}

uint32_t esp_random() {
  static std::random_device rd;
  static std::mutex m;
  std::lock_guard<std::mutex> l(m);
  return rd();
}

// ---------- FreeRTOS ----------

struct SimSemaphore {
  std::timed_mutex m;
};
SemaphoreHandle_t xSemaphoreCreateMutex() { return new SimSemaphore; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
  if (t == portMAX_DELAY) {
    s->m.lock();
    return pdTRUE;
  }
  return s->m.try_lock_for(std::chrono::milliseconds(t)) ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  s->m.unlock();
  return pdTRUE;
}

struct SimQueue {
  size_t len, item;
  std::deque<std::vector<uint8_t>> q;
  std::mutex m;
  std::condition_variable cv;
};
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item) {
  auto* q = new SimQueue;
  q->len = len;
  q->item = item;
  return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
  std::lock_guard<std::mutex> l(q->m);
  if (q->q.size() >= q->len) return pdFALSE;
  auto* p = static_cast<const uint8_t*>(item);
  q->q.emplace_back(p, p + q->item);
  q->cv.notify_one();
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t t) {
  std::unique_lock<std::mutex> l(q->m);
  auto ready = [&] { return !q->q.empty(); };
  if (t == portMAX_DELAY) q->cv.wait(l, ready);
  else if (!q->cv.wait_for(l, std::chrono::milliseconds(t), ready)) return pdFALSE;
  memcpy(item, q->q.front().data(), q->item);
  q->q.pop_front();
  return pdTRUE;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t,
                                   TaskHandle_t*, BaseType_t) {
  std::thread(fn, arg).detach();
  return pdPASS;
}

// ---------- LittleFS ----------

fs::FS LittleFS;

static size_t roundBlock(size_t n) { return (n + 4095) / 4096 * 4096; }

bool fs::FS::begin(bool) {
  const char* r = getenv("SIM_FLASH");
  root_ = r ? r : "./sim-flash";
  stdfs::create_directories(root_);
  return true;
}
std::string fs::FS::real(const char* path) const { return root_ + path; }
size_t fs::FS::usedBytes() const {
  size_t used = 0;
  for (auto& e : stdfs::recursive_directory_iterator(root_)) {
    std::string name = e.path().filename().string();
    // NVS, reloj, botón y tabla ARP del simulador no viven en LittleFS.
    if (name == "nvs.json" || name == "nvs.json.tmp" || name == ".clock_ms" || name == ".boot" || name == ".arp")
      continue;
    used += e.is_regular_file() ? roundBlock(e.file_size()) : 4096;
  }
  return used;
}
fs::File fs::FS::open(const char* path, const char* mode) {
  // SIM_FS_FAIL_PATH=<ruta>: abrir esa ruta para escribir falla (como una flash dañada).
  const char* failPath = getenv("SIM_FS_FAIL_PATH");
  if (failPath && mode[0] != 'r' && strcmp(failPath, path) == 0) return File();
  FILE* f = fopen(real(path).c_str(), mode[0] == 'r' ? "rb" : mode[0] == 'a' ? "ab" : "wb");
  if (!f) return File();
  return File(std::shared_ptr<FILE>(f, fclose), path);
}
bool fs::FS::remove(const char* path) { return ::remove(real(path).c_str()) == 0; }
bool fs::FS::rename(const char* a, const char* b) { return ::rename(real(a).c_str(), real(b).c_str()) == 0; }
bool fs::FS::mkdir(const char* path) { return ::mkdir(real(path).c_str(), 0755) == 0; }

size_t fs::File::read(uint8_t* buf, size_t n) { return f_ ? fread(buf, 1, n, f_.get()) : 0; }
int fs::File::read() { return f_ ? fgetc(f_.get()) : -1; }
size_t fs::File::write(const uint8_t* buf, size_t n) {
  if (!f_) return 0;
  fflush(f_.get());
  if (LittleFS.usedBytes() + n > LittleFS.totalBytes()) return 0;  // flash llena
  return fwrite(buf, 1, n, f_.get());
}
bool fs::File::seek(size_t pos) { return f_ && fseek(f_.get(), long(pos), SEEK_SET) == 0; }
size_t fs::File::size() const {
  if (!f_) return 0;
  fflush(f_.get());
  long cur = ftell(f_.get());
  fseek(f_.get(), 0, SEEK_END);
  long end = ftell(f_.get());
  fseek(f_.get(), cur, SEEK_SET);
  return size_t(end);
}
int fs::File::available() {
  if (!f_) return 0;
  return int(size() - size_t(ftell(f_.get())));
}

// ---------- AsyncUDP: un hilo atiende todos los sockets ----------

static std::mutex gUdpMutex;
static std::vector<AsyncUDP*> gUdp;
static std::once_flag gUdpThread;

static void udpLoop() {
  while (true) {
    std::vector<pollfd> fds;
    std::vector<AsyncUDP*> socks;
    {
      std::lock_guard<std::mutex> l(gUdpMutex);
      for (auto* u : gUdp) socks.push_back(u);
    }
    std::vector<int> ids;
    for (auto* u : socks) {
      int fd = *reinterpret_cast<int*>(u);  // primer miembro: fd_
      fds.push_back({fd, POLLIN, 0});
    }
    if (poll(fds.data(), fds.size(), 100) <= 0) continue;
    for (size_t i = 0; i < fds.size(); i++)
      if (fds[i].revents & POLLIN) socks[i]->dispatch();
  }
}

AsyncUDP::~AsyncUDP() {
  if (fd_ >= 0) close(fd_);
}

bool AsyncUDP::listen(uint16_t port) {
  // El puerto 53 del firmware se mapea a SIM_DNS_PORT (por defecto 5353) para no necesitar root.
  if (port == 53) port = uint16_t(envInt("SIM_DNS_PORT", 5353));
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) return false;
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) return false;
  {
    std::lock_guard<std::mutex> l(gUdpMutex);
    gUdp.push_back(this);
  }
  std::call_once(gUdpThread, [] { std::thread(udpLoop).detach(); });
  return true;
}

void AsyncUDP::onPacket(Handler h) { handler_ = std::move(h); }

void AsyncUDP::dispatch() {
  uint8_t buf[2048];
  sockaddr_in from{};
  socklen_t fl = sizeof from;
  ssize_t n = recvfrom(fd_, buf, sizeof buf, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &fl);
  if (n <= 0 || !handler_) return;
  AsyncUDPPacket p(buf, size_t(n), IPAddress(uint32_t(from.sin_addr.s_addr)), ntohs(from.sin_port));
  handler_(p);
}

size_t AsyncUDP::writeTo(const uint8_t* data, size_t len, const IPAddress addr, uint16_t port) {
  // El puerto 53 propio (autoprueba) es SIM_DNS_PORT en el simulador.
  if (port == 53 && addr == IPAddress(127, 0, 0, 1)) port = uint16_t(envInt("SIM_DNS_PORT", 5353));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = uint32_t(addr);
  ssize_t n = sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
  return n > 0 ? size_t(n) : 0;
}

// ---------- HTTPClient con curl ----------

WiFiClient::WiFiClient(int fd, IPAddress remote)
    : sock_(new int(fd), [](int* p) { if (*p >= 0) close(*p); delete p; }), remote_(remote) {}

int WiFiClient::available() {
  if (f_) {
    int c = fgetc(f_);
    if (c == EOF) return 0;
    ungetc(c, f_);
    return 1024;  // hay datos; readBytes devuelve lo que haya
  }
  if (!sock_ || *sock_ < 0) return 0;
  int n = 0;
  return ioctl(*sock_, FIONREAD, &n) == 0 ? n : 0;
}
int WiFiClient::read(uint8_t* buf, size_t n) {
  if (f_) return int(fread(buf, 1, n, f_));
  if (!sock_ || *sock_ < 0) return -1;
  ssize_t r = recv(*sock_, buf, n, MSG_DONTWAIT);
  return r > 0 ? int(r) : -1;
}
int WiFiClient::readBytes(uint8_t* buf, size_t n) { return read(buf, n); }
size_t WiFiClient::write(const uint8_t* buf, size_t n) {
  if (!sock_ || *sock_ < 0) return 0;
  ssize_t w = ::send(*sock_, buf, n, MSG_NOSIGNAL);
  return w > 0 ? size_t(w) : 0;
}
bool WiFiClient::connected() {
  if (!sock_ || *sock_ < 0) return false;
  char c;
  ssize_t r = recv(*sock_, &c, 1, MSG_PEEK | MSG_DONTWAIT);
  return r > 0 || (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
}
void WiFiClient::stop() { sock_.reset(); }
bool WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeoutMs) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = uint32_t(ip);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    close(fd);
    return false;
  }
  *this = WiFiClient(fd, ip);
  return true;
}

void WiFiServer::begin() {
  if (port_ == 53) port_ = uint16_t(envInt("SIM_DNS_PORT", 5353));
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port_);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || ::listen(fd_, 8) < 0) {
    Serial.printf("[sim] no se pudo abrir TCP %u\n", port_);
    fd_ = -1;
    return;
  }
  fcntl(fd_, F_SETFL, O_NONBLOCK);
}
WiFiClient WiFiServer::available() {
  if (fd_ < 0) return WiFiClient();
  sockaddr_in from{};
  socklen_t fl = sizeof from;
  int c = accept(fd_, reinterpret_cast<sockaddr*>(&from), &fl);
  if (c < 0) return WiFiClient();
  return WiFiClient(c, IPAddress(uint32_t(from.sin_addr.s_addr)));
}

static const char kCloudBase[] = "https://siblock.s-dev.win";

bool HTTPClient::begin(WiFiClient& c, const char* url) {
  client_ = &c;
  url_ = url;
  cloud_ = blocked_ = false;
  if (url_.rfind(kCloudBase, 0) == 0) {
    const char* fake = getenv("SIM_CLOUD_URL");
    cloud_ = true;
    if (fake && *fake) url_ = fake + url_.substr(sizeof kCloudBase - 1);
    else blocked_ = true;  // las pruebas nunca tocan el servidor de verdad
  }
  return url_.rfind("https://", 0) == 0 || url_.rfind("file://", 0) == 0 ||
         url_.rfind("http://127.0.0.1:", 0) == 0;
}

int HTTPClient::GET() { return run("GET", nullptr); }
int HTTPClient::POST(const String& body) { return run("POST", &body.str()); }

int HTTPClient::run(const char* method, const std::string* body) {
  if (cloud_) {
    size_t p = url_.find("://");
    size_t slash = p == std::string::npos ? std::string::npos : url_.find('/', p + 3);
    Serial.printf("[sim] nube %s %s%s\n", method, slash == std::string::npos ? "/" : url_.c_str() + slash,
                  blocked_ ? " (sin SIM_CLOUD_URL: no sale)" : "");
    if (blocked_) return -1;
  } else if (getenv("SIM_HTTP_FAIL")) {
    return -1;
  }
  char tmpl[] = "/tmp/simdl-XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd < 0) return -1;
  close(fd);
  tmp_ = tmpl;
  std::string bodyFile;
  // curl sin shell: los argumentos van separados.
  std::vector<std::string> args = {"curl", "-sS", "-X", method, "--max-time", cloud_ ? "8" : "120",
                                   "-o", tmp_, "-w", "%{http_code}", "-A", userAgent_};
  if (follow_) args.push_back("-L");
  for (auto& h : headers_) {
    args.push_back("-H");
    args.push_back(h.first + ": " + h.second);
  }
  if (body) {
    char btmpl[] = "/tmp/simup-XXXXXX";
    int bfd = mkstemp(btmpl);
    if (bfd < 0) return -1;
    bool ok = ::write(bfd, body->data(), body->size()) == ssize_t(body->size());
    close(bfd);
    bodyFile = btmpl;
    if (!ok) {
      ::remove(bodyFile.c_str());
      return -1;
    }
    args.push_back("--data-binary");
    args.push_back("@" + bodyFile);
  }
  args.push_back(url_);
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
  posix_spawn_file_actions_addclose(&fa, pipefd[0]);
  pid_t pid;
  int rc = posix_spawnp(&pid, "curl", &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(pipefd[1]);
  char out[16] = {0};
  ssize_t r = rc == 0 ? read(pipefd[0], out, sizeof out - 1) : 0;
  (void)r;
  close(pipefd[0]);
  int status = 0;
  if (rc == 0) waitpid(pid, &status, 0);
  if (!bodyFile.empty()) ::remove(bodyFile.c_str());
  int code = atoi(out);
  if (url_.rfind("file://", 0) == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) code = 200;
  if (code == 200) {
    client_->f_ = fopen(tmp_.c_str(), "rb");
    std::error_code ec;
    size_ = int(stdfs::file_size(tmp_, ec));
  }
  return code ? code : -1;
}

bool HTTPClient::connected() { return client_ && client_->f_ && !feof(client_->f_); }

void HTTPClient::end() {
  if (client_ && client_->f_) {
    fclose(client_->f_);
    client_->f_ = nullptr;
  }
  if (!tmp_.empty()) {
    ::remove(tmp_.c_str());
    tmp_.clear();
  }
}

// ---------- WebServer ----------

static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) {
      o += char(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else o += s[i];
  }
  return o;
}

static void parseArgs(const std::string& q, std::map<std::string, std::string>& out) {
  size_t p = 0;
  while (p <= q.size() && !q.empty()) {
    size_t amp = q.find('&', p);
    std::string kv = q.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    size_t eq = kv.find('=');
    if (!kv.empty()) out[urlDecode(kv.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(kv.substr(eq + 1));
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
}

static std::string lower(std::string s) {
  for (auto& c : s) c = char(tolower(c));
  return s;
}

void WebServer::begin() {
  if (port_ == 80) port_ = envInt("SIM_WEB_PORT", 8080);
  listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port_));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || ::listen(listenFd_, 8) < 0) {
    Serial.printf("[sim] no se pudo abrir el puerto web %d\n", port_);
    return;
  }
  fcntl(listenFd_, F_SETFL, O_NONBLOCK);
}

void WebServer::handleClient() {
  if (listenFd_ < 0) return;
  clientFd_ = accept(listenFd_, nullptr, nullptr);
  if (clientFd_ < 0) return;
  timeval tv{2, 0};
  setsockopt(clientFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  std::string req;
  char buf[4096];
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos && req.size() < 16384) {
    ssize_t n = recv(clientFd_, buf, sizeof buf, 0);
    if (n <= 0) break;
    req.append(buf, size_t(n));
    headerEnd = req.find("\r\n\r\n");
  }
  args_.clear();
  headers_.clear();
  extra_.clear();
  sent_ = false;
  if (headerEnd == std::string::npos) {
    close(clientFd_);
    return;
  }
  std::string method, target;
  {
    size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    method = req.substr(0, sp1);
    target = req.substr(sp1 + 1, sp2 - sp1 - 1);
  }
  size_t pos = req.find("\r\n") + 2;
  while (pos < headerEnd) {
    size_t eol = req.find("\r\n", pos);
    std::string line = req.substr(pos, eol - pos);
    size_t c = line.find(':');
    if (c != std::string::npos) {
      std::string v = line.substr(c + 1);
      v.erase(0, v.find_first_not_of(' '));
      headers_[lower(line.substr(0, c))] = v;
    }
    pos = eol + 2;
  }
  {
    sockaddr_in peer{};
    socklen_t pl = sizeof peer;
    getpeername(clientFd_, reinterpret_cast<sockaddr*>(&peer), &pl);
    // SIM_CLIENT_IP permite simular otra IP de la casa (para probar el bloqueo por IP).
    const char* fake = getenv("SIM_CLIENT_IP");
    uint32_t ip = peer.sin_addr.s_addr;
    if (fake && *fake) inet_pton(AF_INET, fake, &ip);
    if (headers_.count("x-sim-client")) inet_pton(AF_INET, headers_["x-sim-client"].c_str(), &ip);
    client_.ip = IPAddress(ip);
  }
  // Mismos topes que lib/WebServer (parcheado): línea de 1024, cuerpo de 4096, sin multipart.
  // Se cierra la conexión sin responder.
  {
    bool bad = false;
    for (size_t a = 0, b; a < headerEnd; a = b + 2) {
      b = req.find("\r\n", a);
      if (b == std::string::npos || b > headerEnd) b = headerEnd;
      if (b - a > 1024) bad = true;
    }
    long cl = headers_.count("content-length") ? atol(headers_["content-length"].c_str()) : 0;
    if (method == "POST" && (cl < 0 || cl > 4096 ||
                             lower(header("Content-Type").c_str()).rfind("multipart/", 0) == 0))
      bad = true;
    if (bad) {
      close(clientFd_);
      clientFd_ = -1;
      return;
    }
  }
  std::string path = target;
  size_t qm = target.find('?');
  if (qm != std::string::npos) path = target.substr(0, qm);
  HTTPMethod m = method == "POST" ? HTTP_POST : HTTP_GET;
  Route* route = nullptr;
  for (auto& r : routes_)
    if (r.path == path && (r.method == HTTP_ANY || r.method == m)) {
      route = &r;
      break;
    }
  size_t want = headers_.count("content-length") ? size_t(atol(headers_["content-length"].c_str())) : 0;
  std::string first = req.substr(headerEnd + 4);
  bool multipart = lower(header("Content-Type").c_str()).rfind("multipart/", 0) == 0;
  if (m == HTTP_POST && route && route->ufn && !multipart) {
    // Modo raw: pedazos de 1436 bytes, sin juntar el cuerpo (como el ESP32).
    raw_.status = RAW_START;
    raw_.totalSize = raw_.currentSize = 0;
    route->ufn();
    raw_.status = RAW_WRITE;
    size_t used = 0;
    while (raw_.totalSize < want) {
      size_t n;
      if (used < first.size()) {
        n = std::min(first.size() - used, sizeof raw_.buf);
        memcpy(raw_.buf, first.data() + used, n);
        used += n;
      } else {
        ssize_t r = recv(clientFd_, raw_.buf, std::min(sizeof raw_.buf, want - raw_.totalSize), 0);
        if (r <= 0) break;
        n = size_t(r);
      }
      raw_.currentSize = n;
      raw_.totalSize += n;
      route->ufn();
    }
    raw_.status = RAW_END;
    raw_.currentSize = 0;
    route->ufn();
  } else {
    std::string body = first;
    while (body.size() < want && want < 65536) {
      ssize_t n = recv(clientFd_, buf, sizeof buf, 0);
      if (n <= 0) break;
      body.append(buf, size_t(n));
    }
    if (qm != std::string::npos) parseArgs(target.substr(qm + 1), args_);
    if (m == HTTP_POST) parseArgs(body, args_);
  }
  bool handled = false;
  if (route) {
    route->fn();
    handled = true;
  }
  if (!handled && notFound_) notFound_();
  if (!sent_) send(500, "text/plain", "sin respuesta");
  close(clientFd_);
  clientFd_ = -1;
}

void WebServer::send(int code, const char* type, const String& body) {
  if (sent_) return;
  sent_ = true;
  std::string r = "HTTP/1.1 " + std::to_string(code) + " X\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + std::to_string(body.length()) +
                  "\r\nConnection: close\r\n" + extra_ + "\r\n" + body.str();
  size_t off = 0;
  while (off < r.size()) {
    ssize_t n = ::send(clientFd_, r.data() + off, r.size() - off, MSG_NOSIGNAL);
    if (n <= 0) break;
    off += size_t(n);
  }
}

String WebServer::arg(const char* name) {
  auto it = args_.find(name);
  return it == args_.end() ? String() : String(it->second);
}

String WebServer::header(const char* name) {
  auto it = headers_.find(lower(name));
  return it == headers_.end() ? String() : String(it->second);
}

static std::string b64decode(const std::string& in) {
  static const std::string k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  unsigned val = 0;
  int bits = -8;
  for (char c : in) {
    size_t p = k.find(c);
    if (p == std::string::npos) break;
    val = ((val << 6) | unsigned(p)) & 0xFFFFFF;
    bits += 6;
    if (bits >= 0) {
      out += char((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out;
}

bool WebServer::authenticate(const char* user, const char* pass) {
  std::string h = header("Authorization").c_str();
  if (h.rfind("Basic ", 0) != 0) return false;
  return b64decode(h.substr(6)) == std::string(user) + ":" + pass;
}

// ---------- Preferences (NVS) en $SIM_FLASH/nvs.json ----------

using NvsMap = std::map<std::string, std::map<std::string, std::string>>;
static std::mutex gNvsMutex;
static NvsMap gNvs;

namespace {
struct JsonReader {
  const std::string& s;
  size_t i = 0;
  bool ok = true;
  void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
  bool eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) { i++; return true; }
    return false;
  }
  static void utf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | (cp >> 6 & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    else { o += char(0xF0 | cp >> 18); o += char(0x80 | (cp >> 12 & 0x3F)); o += char(0x80 | (cp >> 6 & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  }
  uint32_t hex4() {
    if (i + 4 > s.size()) { ok = false; return 0; }
    uint32_t v = uint32_t(strtoul(s.substr(i, 4).c_str(), nullptr, 16));
    i += 4;
    return v;
  }
  std::string str() {
    std::string o;
    if (!eat('"')) { ok = false; return o; }
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { o += c; continue; }
      if (i >= s.size()) break;
      char e = s[i++];
      switch (e) {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            uint32_t lo = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(o, cp);
          break;
        }
        default: o += e;
      }
    }
    if (!eat('"')) ok = false;
    return o;
  }
  std::string value() {  // texto o número (se guarda como texto)
    ws();
    if (i < s.size() && s[i] == '"') return str();
    size_t a = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-')) i++;
    if (a == i) ok = false;
    return s.substr(a, i - a);
  }
};

std::string jsonStr(const std::string& v) {
  std::string o = "\"";
  for (unsigned char c : v) {
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else o += char(c);
  }
  return o + "\"";
}

std::string nvsPath() { return flashDir() + "/nvs.json"; }

void nvsLoad() {
  gNvs.clear();
  FILE* f = fopen(nvsPath().c_str(), "rb");
  if (!f) return;
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  JsonReader r{s};
  NvsMap m;
  if (!r.eat('{')) return;
  if (!r.eat('}')) {
    do {
      std::string ns = r.str();
      if (!r.eat(':') || !r.eat('{')) { r.ok = false; break; }
      auto& kv = m[ns];
      if (!r.eat('}')) {
        do {
          std::string k = r.str();
          if (!r.eat(':')) { r.ok = false; break; }
          kv[k] = r.value();
        } while (r.ok && r.eat(','));
        if (!r.eat('}')) r.ok = false;
      }
    } while (r.ok && r.eat(','));
    if (!r.eat('}')) r.ok = false;
  }
  if (r.ok) gNvs = std::move(m);
  else Serial.println("[sim] nvs.json mal formado: se ignora");
}

bool nvsSave() {
  std::string o = "{";
  bool firstNs = true;
  for (auto& [ns, kv] : gNvs) {
    if (!firstNs) o += ", ";
    firstNs = false;
    o += jsonStr(ns) + ": {";
    bool first = true;
    for (auto& [k, v] : kv) {
      if (!first) o += ", ";
      first = false;
      o += jsonStr(k) + ": " + jsonStr(v);
    }
    o += "}";
  }
  o += "}\n";
  std::string tmp = nvsPath() + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(o.data(), 1, o.size(), f) == o.size();
  ok = fclose(f) == 0 && ok;
  return ok && ::rename(tmp.c_str(), nvsPath().c_str()) == 0;
}

// Al arrancar el simulador, nvs.json existe aunque esté vacío (como la partición NVS).
struct NvsInit {
  NvsInit() {
    stdfs::create_directories(flashDir());
    struct stat st;
    if (stat(nvsPath().c_str(), &st) != 0) {
      if (FILE* f = fopen(nvsPath().c_str(), "wb")) {
        fputs("{}\n", f);
        fclose(f);
      }
    }
  }
} gNvsInit;

std::string toHexStr(const uint8_t* p, size_t n) {
  std::string o;
  char b[3];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof b, "%02x", p[i]);
    o += b;
  }
  return o;
}
}  // namespace

bool Preferences::begin(const char* ns, bool readOnly) {
  if (!ns || !*ns || strlen(ns) > 15) return false;
  std::lock_guard<std::mutex> l(gNvsMutex);
  nvsLoad();
  ns_ = ns;
  open_ = true;
  readOnly_ = readOnly;
  return true;
}
void Preferences::end() { open_ = false; }

static bool keyOk(const char* key) { return key && *key && strlen(key) <= 15; }

bool Preferences::isKey(const char* key) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || !keyOk(key)) return false;
  auto it = gNvs.find(ns_);
  return it != gNvs.end() && it->second.count(key);
}

String Preferences::getString(const char* key, const String& def) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || !keyOk(key)) return def;
  auto it = gNvs.find(ns_);
  if (it == gNvs.end()) return def;
  auto kv = it->second.find(key);
  return kv == it->second.end() ? def : String(kv->second);
}

size_t Preferences::putString(const char* key, const String& value) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || readOnly_ || !keyOk(key) || value.length() > 4000) return 0;
  // SIM_NVS_FAIL_KEY=<clave>: guardar esa clave falla (como una NVS llena o dañada).
  const char* failKey = getenv("SIM_NVS_FAIL_KEY");
  if (failKey && strcmp(failKey, key) == 0) return 0;
  gNvs[ns_][key] = value.str();
  return nvsSave() ? value.length() : 0;
}

static bool decodeHexValue(const std::string& v, std::string& out) {
  if (v.rfind("hex:", 0) != 0 || (v.size() - 4) % 2) return false;
  out.clear();
  for (size_t i = 4; i < v.size(); i += 2) {
    char* end;
    std::string h = v.substr(i, 2);
    long b = strtol(h.c_str(), &end, 16);
    if (*end) return false;
    out += char(b);
  }
  return true;
}

size_t Preferences::getBytesLength(const char* key) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || !keyOk(key)) return 0;
  auto it = gNvs.find(ns_);
  if (it == gNvs.end() || !it->second.count(key)) return 0;
  std::string b;
  return decodeHexValue(it->second[key], b) ? b.size() : 0;
}

size_t Preferences::getBytes(const char* key, void* buf, size_t maxLen) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || !keyOk(key)) return 0;
  auto it = gNvs.find(ns_);
  if (it == gNvs.end() || !it->second.count(key)) return 0;
  std::string b;
  if (!decodeHexValue(it->second[key], b) || b.size() > maxLen) return 0;
  memcpy(buf, b.data(), b.size());
  return b.size();
}

size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || readOnly_ || !keyOk(key)) return 0;
  gNvs[ns_][key] = "hex:" + toHexStr(static_cast<const uint8_t*>(value), len);
  return nvsSave() ? len : 0;
}

uint32_t Preferences::getUInt(const char* key, uint32_t def) {
  String v = getString(key);
  if (v.isEmpty()) return def;
  char* end;
  unsigned long n = strtoul(v.c_str(), &end, 10);
  return *end ? def : uint32_t(n);
}

size_t Preferences::putUInt(const char* key, uint32_t value) {
  return putString(key, String(std::to_string(value))) ? 4 : 0;
}

bool Preferences::remove(const char* key) {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || readOnly_ || !keyOk(key)) return false;
  auto it = gNvs.find(ns_);
  if (it == gNvs.end() || !it->second.erase(key)) return false;
  return nvsSave();
}

bool Preferences::clear() {
  std::lock_guard<std::mutex> l(gNvsMutex);
  if (!open_ || readOnly_) return false;
  gNvs.erase(ns_);
  return nvsSave();
}

// ---------- WiFi: AP, escaneo y SIM_WIFI=down ----------

static bool wifiDown() {
  const char* w = getenv("SIM_WIFI");
  return w && strcmp(w, "down") == 0;
}

void WiFiClass::begin(const char* ssid, const char*) { simOk_ = ssid && strcmp(ssid, "simulador-ok") == 0; }

String WiFiClass::SSID() {
  if (simOk_) return String("simulador-ok");
  return wifiDown() ? String() : String("CasaDemo");
}

wl_status_t WiFiClass::status() {
  if (!wifiDown()) return WL_CONNECTED;
  return simOk_ ? WL_CONNECTED : WL_DISCONNECTED;
}

bool WiFiClass::softAP(const char* ssid, const char*) {
  Serial.printf("[sim] AP %s\n", ssid ? ssid : "");  // la contraseña nunca se imprime
  return true;
}

bool WiFiClass::softAPConfig(IPAddress ip, IPAddress, IPAddress) {
  Serial.printf("[sim] AP en %s\n", ip.toString().c_str());
  return true;
}

bool WiFiClass::softAPdisconnect(bool) {
  Serial.println("[sim] AP apagado");
  return true;
}

IPAddress WiFiClass::gatewayIP() {
  const char* g = getenv("SIM_GATEWAY");
  uint32_t ip = 0;
  if (g && *g && inet_pton(AF_INET, g, &ip) == 1) return IPAddress(ip);
  return IPAddress(127, 0, 0, 254);
}

uint8_t WiFiClass::softAPgetStationNum() { return uint8_t(envInt("SIM_AP_STATIONS", 0)); }

uint8_t* WiFiClass::macAddress(uint8_t* mac) {
  static const uint8_t kMac[6] = {0x24, 0x6f, 0x28, 0xab, 0x0c, 0x3f};
  memcpy(mac, kMac, 6);
  return mac;
}

namespace {
struct SimNet {
  const char* ssid;
  int32_t rssi;
  wifi_auth_mode_t auth;
};
const SimNet kNets[] = {{"CasaDemo", -50, WIFI_AUTH_WPA2_PSK},
                        {"Caf\xc3\xa9 \"\xc3\x91" "and\xc3\xba\"", -70, WIFI_AUTH_OPEN}};
}  // namespace

int16_t WiFiClass::scanNetworks() {
  Serial.println("[sim] escaneo de redes");  // las pruebas cuentan cuántas veces se escanea
  return int16_t(sizeof kNets / sizeof kNets[0]);
}
String WiFiClass::SSID(uint8_t i) { return i < 2 ? String(kNets[i].ssid) : String(); }
int32_t WiFiClass::RSSI(uint8_t i) { return i < 2 ? kNets[i].rssi : 0; }
wifi_auth_mode_t WiFiClass::encryptionType(uint8_t i) { return i < 2 ? kNets[i].auth : WIFI_AUTH_OPEN; }
