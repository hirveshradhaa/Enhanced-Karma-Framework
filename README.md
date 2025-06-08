# Enhanced Karma Framework

This repository contains an implementation of the Karma algorithm as a C++ library for integration with applications requiring resource allocation. The library provides an allocator module with a simple and general interface that takes user demands as input and computes allocations using the Karma algorithm.

---

## Project Overview

The Enhanced Karma Framework aims to:

- Implement a flexible, efficient allocator for resource allocation in systems that share resources among multiple users.
- Provide an easy-to-use C++ API for initializing users, updating demands, and retrieving allocations.
- Optimize the allocation process using a batched approach to improve performance in real-world applications.

This system is intended for research and practical use cases where fair and efficient allocation of resources (e.g., CPU, memory, bandwidth) is essential.

---

## Key Features

- Karma algorithm implementation for proportional resource allocation.
- Support for adding and removing users dynamically.
- Batched allocation to efficiently compute user shares.
- Modular design to integrate easily with other systems.

---

## Technologies Used

- C++ — for the core algorithm and allocator module.
- CMake — for cross-platform build and project management.
- Visual Studio Code (optional) — for development and debugging.

---

## Getting Started

### Prerequisites

- CMake version 3.12 or later
- A C++ compiler supporting C++11 or later

### Installation

1. Clone the repository:
    ```bash
    git clone https://github.com/yourusername/Enhanced-Karma-Framework.git
    cd Enhanced-Karma-Framework
    ```

2. Build the library:
    ```bash
    mkdir build
    cd build
    cmake ..
    make
    ```

---

## Usage

To use the allocator module, include `include/karma_allocator.h` in your source files and link `libkarma` (generated in the `build/` directory) to your executable.

Example usage:

```cpp
// Initialize Karma allocator with 6 slices and alpha=0.5
karma_allocator allocator(6, 0.5);

// Add users
allocator.add_user("A");
allocator.add_user("B");
allocator.add_user("C");

// Update demands
allocator.update_demand("A", 4);
allocator.update_demand("B", 1);
allocator.update_demand("C", 1);

// Compute allocations
allocator.allocate();

// Output allocations
std::cout << "A: " << allocator.get_allocation("A") << " ";
std::cout << "B: " << allocator.get_allocation("B") << " ";
std::cout << "C: " << allocator.get_allocation("C") << std::endl;
```

For a full example, refer to the `example/` directory in the project.

---

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request for any bug fixes, new features, or improvements.

---

## Additional Notes

- Visual Studio Code workspace configuration is provided in `.vscode/` for easier development.
- Additional examples and test cases can be added in the `example/` folder.
- Future improvements may include support for additional allocation algorithms and extended metrics.

---
