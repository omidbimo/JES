# JSON for Embedded Systems (JES)

**JES** is a lightweight, resource-constrained [JSON](https://www.json.org/json-en.html) library engineered specifically for embedded applications operating under strict hardware limitations, such as limited stack size and no dynamic memory allocation. The library maintains full compliance with the [ECMA-404](https://ecma-international.org/publications-and-standards/standards/ecma-404) JSON standard, ensuring interoperability with standard JSON data while operating efficiently within the constraints of resource-limited environment.

## Overview

JES provides a simple and efficient API to:

- Parse a JSON string into a tree of JSON elements.
- Modify the elements in place.
- Serialize the tree back into a JSON string.

## Key Features

- ✅ **No dynamic memory allocation** – All objects are stored in a single working buffer, ensuring predictable memory usage.
- ✅ **Non-recursive parser** – Designed to handle deep JSON structures without relying on recursion, making it suitable for environments with limited stack size.
- ✅ **Concurrent document handling** – Supports parsing multiple JSON documents simultaneously by using a separate parser context and working buffer for each document.
- ✅ **High performance** – Operates directly on the input buffer without copying data, ensuring minimal processing overhead.
- ✅ **No external dependencies** – Fully self-contained, making it easy to integrate into embedded projects.
- ✅ **Fast Key search**  – Can be configured at compile time to utilise a hash table key lookup for faster key searching in large JSON data.
- ✅ **Flexible output formatting** – The output can be generated as a compact string or formatted with indentation for readability.

## [API Documentation](https://github.com/omidbimo/JES/blob/main/documentation.md)

## Limitations

While JES is designed to be a robust and efficient JSON parser for embedded and resource-constrained environments, it has certain limitations that should be taken into account when evaluating it for your project.

JES is optimized for small JSON documents (typically a few kilobytes) used in configuration or control systems. It is not intended for parsing or manipulating large, complex JSON structures. Below are key limitations to be aware of:

- **No Dynamic Memory Allocation** - JES avoids dynamic memory allocation and works with user-provided memory buffers. This has implications:
  
  - The user must pre-allocate enough memory based on an estimate of the maximum JSON size.
  
  - When creating or modifying JSON elements, the user is responsible for storing and maintaining any new string values.

- **Not Thread-Safe** - JES does not provide internal synchronization and is not safe for use across multiple threads unless externally protected.

- **Limited JSON Feature Support** - JES focuses on core JSON parsing and does not currently support advanced features such as JSON Pointer, JSONPath, or other modern JSON querying standards.
