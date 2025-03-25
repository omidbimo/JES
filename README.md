# JSON for Embedded Systems (JES)  
**JES** is a lightweight JSON library designed specifically for embedded systems with strict resource constraints, such as limited stack size and no dynamic memory allocation. The goal is to make JES fully conform to the [RFC 8259 JSON standard](https://tools.ietf.org/html/rfc8259).

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
- ✅ **Configurable key handling** – Can be configured to support or overwrite duplicate keys.  
- ✅ **Flexible output formatting** – The output can be generated as a compact string or formatted with indentation for readability.
