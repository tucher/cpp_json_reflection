# Struct Composition with Flatten and Inheritance

> **NOTE:** All features on this page require C++26 reflection (`-std=c++26 -freflection` in GCC 16+). They are not available in the Boost.PFR fallback path.

## Problem

You have composed or inherited C++ types and want their fields to appear in a single flat JSON object — without manual field forwarding.

## Inheritance Flattening (automatic)

Base class fields are automatically included at the derived class's JSON level. No annotation needed:

```cpp
struct Identifiable {
    int id;
    std::string name;
};

struct User : Identifiable {
    std::string email;
};
// JSON: {"id": 1, "name": "Alice", "email": "alice@example.com"}
```

Multi-level and multiple inheritance both work:

```cpp
struct Timestamps {
    std::string created_at;
    std::string updated_at;
};

struct AuditedUser : User, Timestamps {
    std::string role;
};
// JSON: {"id": 1, "name": "Alice", "email": "...", "created_at": "...", "updated_at": "...", "role": "admin"}
```

If a derived class declares a field with the same name as a base class field, the derived field wins.

## Explicit Composition Flattening (`flatten`)

Use `flatten` to promote a composed field's members to the parent JSON level:

```cpp
struct Address {
    std::string city;
    std::string zip;
};

// C++26 annotation syntax
struct Person {
    std::string name;
    [[=A<flatten>{}]] Address address;
};

// Wrapper syntax (equivalent)
struct Person {
    std::string name;
    A<Address, flatten> address;
};

// JSON: {"name": "Alice", "city": "NYC", "zip": "10001"}
```

### Multiple Flattened Fields

```cpp
struct Contact { std::string email; std::string phone; };
struct Geo { double lat; double lon; };

struct Record {
    int id;
    [[=A<flatten>{}]] Contact contact;
    [[=A<flatten>{}]] Geo location;
};
// JSON: {"id": 1, "email": "...", "phone": "...", "lat": 40.7, "lon": -74.0}
```

### Validators on Inner Fields

Inner fields retain their own validators:

```cpp
struct ServerConfig {
    [[=A<range<1, 65535>>{}]] int port;
    [[=A<range<1, 100>>{}]] int max_connections;
};

struct Server {
    std::string host;
    [[=A<flatten>{}]] ServerConfig config;
};
// JSON: {"host": "localhost", "port": 8080, "max_connections": 50}
// port and max_connections are still validated through their range<> annotations
```

### Combining Flatten with Inheritance

Flatten and inheritance compose naturally:

```cpp
struct BaseServer {
    std::string host;
    [[=A<flatten>{}]] ServerConfig config;
};

struct ProductionServer : BaseServer {
    int priority;
};
// JSON: {"host": "...", "port": 8080, "max_connections": 50, "priority": 1}
```

## See Also

- [Annotations Reference — Composition Options](../ANNOTATIONS_REFERENCE.md)
- [Create reusable templated submodels](composable-reusable-types.md)

---

[Back to How-to Guides](index.md) | [Back to Documentation](../index.md)
