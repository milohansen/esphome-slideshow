## 2025-05-23 - [Bitmask Optimization for Slot Management]
**Learning:** For small fixed-size resources (like image slots in an embedded system), using a bitmask (uint64_t) is significantly more efficient than nested loops over maps. It reduces complexity from O(S*L) to O(S+L) and avoids multiple pointer dereferences.
**Action:** Always consider bitmasks for tracking availability of a small, fixed number of resources.

## 2025-05-23 - [Stack vs Heap for Sliding Windows]
**Learning:** Using `std::set` for a tiny number of items (e.g., a window of 3 elements) in a frequently called function introduces unnecessary heap allocation/deallocation overhead. A fixed-size array on the stack is much faster.
**Action:** Replace small `std::set` or `std::vector` with fixed-size stack arrays when the maximum size is known and small.
