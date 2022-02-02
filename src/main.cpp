#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include "cxxopts.hpp"

// clang-format off
#include "glad.h"
#include <GLFW/glfw3.h>
// clang-format on

unsigned kChildBootWaitExtraMs = 300;
std::string g_program_path;

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  glViewport(0, 0, width, height);
}

/** Initializes GL and an X window, makes the window the current GL context. */
void glfw_init(std::string window_name) {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window =
      glfwCreateWindow(800, 600, window_name.c_str(), NULL, NULL);
  if (window == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    exit(1);
  }
  glfwMakeContextCurrent(window);
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    exit(1);
  }
  glViewport(0, 0, 800, 600);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}

void allocate_gpu_memory(size_t num_bytes) {
  unsigned int buf;
  glGenBuffers(1, &buf);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buf);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, num_bytes, NULL, GL_STATIC_DRAW);
  uint8_t data = 1;
  glClearBufferData(GL_PIXEL_UNPACK_BUFFER, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
                    &data);
  glFinish();
}

/**
 * Repeatedly forks a process and allocates the memory in the child, then cleans
 * up.
 *
 * NOTE: Emperically, using fork() is the only sure-fire way to make GPU memory
 * truly oscillate. Any attempt to make GPU memory oscillate within the same
 * process is subject to NVIDIA's blackbox GL implementation. NVIDIA's GL
 * memory allocator is smart, so it's hard to get a real oscillating effect; the
 * allocator will often reserve memory even after an OpenGL command to
 * "invalidate" the memory.
 */
void run_oscillating_allocations(unsigned oscillate_mib,
                                 unsigned oscillate_time_ms) {
  while (true) {
    std::cout << "Oscillating memory allocating..." << std::endl;
    pid_t child_pid = fork();
    if (child_pid == 0) {
      const char *child_args[] = {g_program_path.c_str(), "-m",
                                  std::to_string(oscillate_mib).c_str(), NULL};
      execvp(g_program_path.c_str(), (char *const *)child_args);
    } else {
      std::this_thread::sleep_for(
          std::chrono::milliseconds{oscillate_time_ms + kChildBootWaitExtraMs});
      kill(child_pid, SIGTERM);
      std::cout << "Oscillating memory freed" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{oscillate_time_ms});
  }
}

int main(int argc, char *argv[]) {
  g_program_path = argv[0];
  cxxopts::Options options(
      argv[0],
      "Allocates GPU memory for memory pressure testing.\n"
      "Depends on OpenGL and libglfw (sudo apt install libglfw3-dev)");

  // clang-format off
  options.add_options()
    ("m,mib", "MiB to allocate", cxxopts::value<unsigned>())
    ("o,oscillate-mib", "MiB to allocate in an oscillating way", cxxopts::value<unsigned>()->default_value("0"))
    ("t,oscillate-time-ms", "How quickly to oscillate memory, in milliseconds", cxxopts::value<unsigned>()->default_value("500"))
    ("h,help", "Print usage")
  ;
  // clang-format on

  auto parsed = options.parse(argc, argv);
  if (parsed.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  auto mib_size = parsed["mib"].as<unsigned>();
  if (mib_size <= 12) {
    std::cerr << "Allocation must be larger than 12MiB" << std::endl;
    return -1;
  }
  mib_size -= 12;
  size_t gpu_mem_size_bytes = 1024 * 1024 * mib_size;

  auto oscillate_mib = parsed["oscillate-mib"].as<unsigned>();
  if (oscillate_mib != 0 && oscillate_mib <= 12) {
    std::cerr << "Oscillation allocation must be larger than 12MiB"
              << std::endl;
    return -1;
  }

  glfw_init("Allocate GPU memory base");
  allocate_gpu_memory(gpu_mem_size_bytes);

  if (oscillate_mib == 0) {
    sleep(std::numeric_limits<unsigned int>::max());
  } else {
    auto oscillate_time_ms = parsed["oscillate-time-ms"].as<unsigned>();
    run_oscillating_allocations(oscillate_mib, oscillate_time_ms);
  }

  glfwTerminate();
  return 0;
}
