# Arche ECS — Modernization & Template Cleanup Specification

> **Document Status:** Codebase Modernization Proposal & Refactoring Guide  
> **Directives Compliance:** Zero virtual methods, no RTTI, flat Data-Oriented Design (DOD), pure compile-time C++20 idioms, 100% binary/performance equivalence.

---

## 1. Executive Summary & Motivation

In C++20, many of the classical metaprogramming patterns from C++11 and C++14 (such as `std::false_type` inheritance, recursive template instantiations, verbose unpacker structs, and multi-specialization type resolvers) can be replaced with **direct, zero-overhead language constructs**:
* **Variable Template Specializations:** Replace 7-line trait structs with 2-line `constexpr bool` variables.
* **C++20 Template Parameter Lambdas:** Direct pack expansion `[&]<typename... Ts>(Tag<Ts...>)` eliminates entire helper structs like `with_unpacker`.
* **Flat Compile-Time Arrays:** Replace recursive template instantiations (`1 + index_in_pack<...>`) with $O(1)$ compile-time array scans.
* **Clean Type Aliases (`std::conditional_t`):** Eliminate 20-line multi-struct type resolvers like `query_element` and `column_pointer_type`.
* **`constexpr` Tag Objects:** Eliminate ugly empty braces `{}` in user filter calls (`without<Dead>` instead of `Without<Dead>{}`).

This document inventories every redundant, verbose, or legacy template construct across the codebase and provides the exact, drop-in modern replacements that preserve **100% identical runtime behavior and performance**.

---

## 2. Filter Traits Modernization (`include/arche/types/filters.hpp`)

### 2.1 Problem: Verbose `std::false_type` SFINAE Structs
Currently, `include/arche/types/filters.hpp` defines type traits using the C++11 `std::false_type` pattern:

```cpp
// CURRENT CODE (Lines 82-110 in include/arche/types/filters.hpp):
template <typename T> struct is_with_filter : std::false_type {};
template <typename... Ts> struct is_with_filter<With<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_with_filter_v = is_with_filter<T>::value;

template <typename T> struct is_without_filter : std::false_type {};
template <typename... Ts> struct is_without_filter<Without<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_without_filter_v = is_without_filter<T>::value;

template <typename T> struct is_has : std::false_type {};
template <typename T> struct is_has<Has<T>> : std::true_type { using type = T; };
template <typename T> inline constexpr bool is_has_v = is_has<std::decay_t<T>>::value;

template <typename T> struct is_or_filter : std::false_type {};
template <typename... Filters> struct is_or_filter<Or<Filters...>> : std::true_type {};
template <typename T> inline constexpr bool is_or_filter_v = is_or_filter<std::decay_t<T>>::value;
```

### 2.2 Solution: 2-Line C++20 Variable Templates
In modern C++, variable templates can be directly specialized without declaring a struct:

```cpp
// PROPOSED MODERN REPLACEMENT:
template <typename T> inline constexpr bool is_with_v = false;
template <typename... Ts> inline constexpr bool is_with_v<With<Ts...>> = true;

template <typename T> inline constexpr bool is_without_v = false;
template <typename... Ts> inline constexpr bool is_without_v<Without<Ts...>> = true;

template <typename T> inline constexpr bool is_has_v = false;
template <typename T> inline constexpr bool is_has_v<Has<T>> = true;

template <typename T> struct has_inner { using type = void; };
template <typename T> struct has_inner<Has<T>> { using type = T; };
template <typename T> using has_inner_t = typename has_inner<std::decay_t<T>>::type;

template <typename T> inline constexpr bool is_or_v = false;
template <typename... Filters> inline constexpr bool is_or_v<Or<Filters...>> = true;
```
* **Lines of code:** 40 lines $\rightarrow$ 12 lines.
* **Compilation impact:** Faster build times (fewer type symbols in symbol table).
* **Runtime impact:** Exactly 0 (resolved entirely at compile time).

---

## 3. Deleting Unpacker Structs (`src/query/filter.hpp`)

### 3.1 Problem: Redundant `with_unpacker` / `without_unpacker`
Currently, `src/query/filter.hpp` and `src/spatial/sort.hpp` declare helper structs just to crack open the type pack `Ts...` inside `With<Ts...>`:

```cpp
// CURRENT CODE (Lines 56-78 in src/query/filter.hpp):
template <typename T> struct with_unpacker;
template <typename... Ts> struct with_unpacker<With<Ts...>> {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    ([&] {
      reg.template register_type<std::decay_t<Ts>>();
      mask.set(type_id<std::decay_t<Ts>>());
    }(), ...);
  }
};

template <typename T> struct without_unpacker;
template <typename... Ts> struct without_unpacker<Without<Ts...>> {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    ([&] {
      reg.template register_type<std::decay_t<Ts>>();
      mask.set(type_id<std::decay_t<Ts>>());
    }(), ...);
  }
};
```

### 3.2 Solution: Direct Inline Extraction via C++20 Template Lambdas
C++20 introduces templated lambdas with explicit template parameter lists (`[&]<typename... Ts>(...)`). We can crack open `With<Ts...>` directly inline in a single statement:

```cpp
// PROPOSED MODERN REPLACEMENT:
template <typename Filter>
inline void apply_filter_to_mask(ComponentMask &mask, TypeRegistry &reg, Filter filter = {}) {
    if constexpr (is_with_v<Filter>) {
        [&]<typename... Ts>(With<Ts...>) {
            ((reg.template register_type<std::decay_t<Ts>>(), mask.set(type_id<std::decay_t<Ts>>())), ...);
        }(filter);
    } else if constexpr (is_without_v<Filter>) {
        [&]<typename... Ts>(Without<Ts...>) {
            ((reg.template register_type<std::decay_t<Ts>>(), mask.set(type_id<std::decay_t<Ts>>())), ...);
        }(filter);
    }
}
```
* **Result:** `with_unpacker` and `without_unpacker` can be **completely deleted**.
* Call sites in `src/spatial/sort.hpp` simplify from:
  ```cpp
  // Old:
  with_unpacker<Filter>::apply(include_mask, internal::WorldAccess::registry(world));
  // New:
  apply_filter_to_mask(include_mask, internal::WorldAccess::registry(world), Filter{});
  ```

---

## 4. Query Element & Column Pointer Deduction (`include/arche/types/filters.hpp`)

### 4.1 Problem: Multi-Struct Specializations
Currently, 32 lines are spent specializing `query_element` and `column_pointer_type` across raw types, pointer types, and `Has<T>`:

```cpp
// CURRENT CODE (Lines 117-150 in include/arche/types/filters.hpp):
template <typename T> struct query_element { using type = T &; };
template <typename T> requires(std::is_pointer_v<T>) struct query_element<T> { using type = T; };
template <typename T> struct query_element<Has<T>> { using type = bool; };
template <typename T> using query_element_t = typename query_element<T>::type;

template <typename T> struct column_pointer_type { using type = T *; };
template <typename T> requires(std::is_pointer_v<T>) struct column_pointer_type<T> { using type = T; };
template <typename T> struct column_pointer_type<Has<T>> { using type = bool; };
template <typename T> using column_pointer_type_t = typename column_pointer_type<T>::type;
```

### 4.2 Solution: Clean `std::conditional_t` Aliases
```cpp
// PROPOSED MODERN REPLACEMENT:
template <typename T>
using query_element_t = std::conditional_t<
    is_has_v<std::decay_t<T>>, bool,
    std::conditional_t<std::is_pointer_v<std::decay_t<T>>, std::decay_t<T>, T&>
>;

template <typename T>
using column_pointer_type_t = std::conditional_t<
    is_has_v<std::decay_t<T>>, bool,
    std::conditional_t<std::is_pointer_v<std::decay_t<T>>, std::decay_t<T>, T*>
>;
```
* Eliminates 6 template struct definitions.
* Reads linearly as a single conditional ternary expression.

---

## 5. Eliminating Recursive Template Instantiation in `index_in_pack`

### 5.1 Problem: Recursive Template Instantiation
In `include/arche/query.hpp` (lines 178–185):
```cpp
// CURRENT CODE:
template <typename Target, typename First, typename... Rest>
static constexpr size_t index_in_pack() {
  if constexpr (std::is_same_v<std::decay_t<Target>, std::decay_t<First>>) {
    return 0;
  } else {
    return 1 + index_in_pack<Target, Rest...>();
  }
}
```
For queries with 10 components, the compiler instantiates 10 nested copies of `index_in_pack`, increasing compiler template depth and compile times.

### 5.2 Solution: Compile-Time Flat Array Scan (Zero Recursion)
In C++20, an array initialized with a fold expression and scanned in a `constexpr` loop compiles in 1 step without recursive template instantiations:

```cpp
// PROPOSED MODERN REPLACEMENT:
template <typename Target, typename... Ts>
static constexpr size_t index_in_pack() noexcept {
    constexpr bool matches[] = { std::is_same_v<std::decay_t<Target>, std::decay_t<Ts>>... };
    for (size_t i = 0; i < sizeof...(Ts); ++i) {
        if (matches[i]) return i;
    }
    return sizeof...(Ts);
}
```
* **Zero recursive template instantiations.**
* Fully evaluated at compile-time by all major compilers.

---

## 6. Eliminating Duplicate Offset Resolvers

### 6.1 Problem: Code Duplicated Across Headers
There are currently **two identical implementations** of column offset resolution:
1. `Query<Components...>::resolve_column_offset<Comp>(arch)` in `src/query/query.inl` (lines 345–369).
2. `resolve_query_offset<Comp>(arch)` in `src/query/filter.hpp` (lines 14–41).

Both test for `Entity`, `Has<T>`, pointer, and normal component layout offset.

### 6.2 Solution: Unify into `Archetype::column_offset<T>()` and `filter.hpp`
- Pure component layout offset belongs on `Archetype::column_offset<T>()` in `src/archetype/archetype.hpp`.
- Query parameter resolution (`Has<T>`, optional pointers, `Entity`) is defined **once** in `src/query/filter.hpp` as `resolve_query_offset<Comp>(arch)`.
- `Query::resolve_column_offset` simply forwards to `resolve_query_offset<Comp>(arch)` or is replaced by it.

---

## 7. Consolidating `Or<...>` Branch Builders

### 7.1 Problem: 4 Fragmented Member Functions
In `include/arche/query.hpp` (lines 229–254):
```cpp
template <typename... SubFilters> inline void apply_filter(Or<SubFilters...>);
template <typename BranchType> inline void build_or_branch(OrBranch &branch, BranchType);
template <typename... Ts> inline void apply_branch_with(OrBranch &branch, With<Ts...>);
template <typename... Ts> inline void apply_branch_without(OrBranch &branch, Without<Ts...>);
```
Having 4 separate methods clutter the class declaration just to build an OR branch clause.

### 7.2 Solution: Modern Fold Lambda
All 4 functions can be collapsed into a single concise method inside `query.inl`:

```cpp
// PROPOSED MODERN REPLACEMENT:
template <typename... Components>
template <typename... SubFilters>
inline void Query<Components...>::apply_filter(Or<SubFilters...>) {
    OrClause clause;
    auto add_branch = [&](auto branch_tag) {
        using B = decltype(branch_tag);
        OrBranch branch;
        if constexpr (is_with_v<B>) {
            [&]<typename... Ts>(With<Ts...>) { register_and_set_mask<Ts...>(branch.include); }(branch_tag);
        } else if constexpr (is_without_v<B>) {
            [&]<typename... Ts>(Without<Ts...>) { register_and_set_mask<Ts...>(branch.exclude); }(branch_tag);
        } else {
            register_and_set_mask<B>(branch.include);
        }
        clause.branches.push_back(std::move(branch));
    };
    (add_branch(SubFilters{}), ...);
    or_clauses_.push_back(std::move(clause));
}
```
* **Deletes 3 member functions** (`build_or_branch`, `apply_branch_with`, `apply_branch_without`) from `Query`.

---

## 8. User-Facing Tag Syntax Modernization (`constexpr` Tags)

### 8.1 Problem: Awkward Empty Braces `{}` in User Code
Currently, callers must pass empty struct instances with `{}`:
```cpp
world.query<Position>(Without<Dead>{}, With<Player>{});
```

### 8.2 Solution: Lowercase `constexpr` Tag Variables
Define global `constexpr` variable templates in `include/arche/types/filters.hpp`:

```cpp
// Global constexpr tags (similar to std::views in C++20):
template <typename... Ts> inline constexpr With<Ts...> with{};
template <typename... Ts> inline constexpr Without<Ts...> without{};
```

Now users can write clean, natural, lowercase syntax with **zero empty braces**:
```cpp
// Clean, modern syntax:
world.query<Position>(without<Dead>, with<Player>);
```
*(Both `without<Dead>` and `Without<Dead>{}` continue to work interchangeably for full backward compatibility).*

---

## 9. Comprehensive Modernization Checklist

| Area | Current Implementation | Proposed C++20 Replacement | Files Affected |
| :--- | :--- | :--- | :--- |
| **Filter Traits** | SFINAE `struct is_xxx : std::false_type` | `inline constexpr bool is_xxx_v = false;` | `include/arche/types/filters.hpp` |
| **Pack Unpackers** | `struct with_unpacker` / `without_unpacker` | Inline lambda `[&]<typename... Ts>(With<Ts...>)` | `src/query/filter.hpp`, `src/spatial/sort.hpp` |
| **Type Resolvers** | `struct query_element`, `column_pointer_type` | `std::conditional_t` type aliases | `include/arche/types/filters.hpp` |
| **Pack Index Search**| Recursive `1 + index_in_pack<...>()` | Flat `constexpr bool matches[]` loop | `include/arche/query.hpp`, `src/query/query.inl` |
| **Offset Resolution**| Duplicate `resolve_query_offset` and `resolve_column_offset` | Unified single `resolve_query_offset` | `src/query/filter.hpp`, `src/query/query.inl` |
| **OR Clause Building**| 4 helper functions in `Query` | Single inline fold lambda | `include/arche/query.hpp`, `src/query/query.inl` |
| **Caller Tag Syntax**| Mandatory `{}`: `Without<T>{}` | Optional `constexpr`: `without<T>` | `include/arche/types/filters.hpp` |
| **Prefetch Intrinsics**| Hardcoded `__builtin_prefetch` | Centralized `Hardware::prefetch_read` | `src/hardware/hardware.hpp`, `src/query/query.inl` |
