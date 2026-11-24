module;

#include <cstddef>
#include <cstdint>
#include <span>

export module arche:chunk;

export import :entity;
export import :component.detail.column_layout;
export import :chunk.detail.chunk_header;

export namespace arche {

// =============================================================================
// Relocation Records (Delta Stream)
// =============================================================================

/**
 * @struct ChunkRelocation
 * @brief Records an entity handle relocation resulting from a single
 * swap-and-pop deletion.
 */
struct ChunkRelocation {
  /// Handle of the entity that was relocated from the tail into the vacated row
  /// slot.
  Entity moved_entity{};

  /// Returns true if an entity was relocated during the swap-and-pop operation.
  [[nodiscard]] constexpr bool has_relocation() const noexcept {
    return !moved_entity.is_null();
  }
};

/**
 * @struct EntityRelocation
 * @brief Records a batch relocation pair mapping an entity handle to its new
 * row index.
 */
struct EntityRelocation {
  /// Entity handle that was relocated.
  Entity entity{};

  /// New zero-based row index of the entity inside the chunk.
  uint32_t new_row{0};
};

// =============================================================================
// Hardware-Adaptive Chunk Storage (Structure of Arrays)
// =============================================================================

/**
 * @struct Chunk
 * @brief 64KB page-aligned Structure-of-Arrays (SoA) memory block storing
 * archetype entity rows.
 *
 * Chunks are the fundamental physical storage unit in Arche ECS. Each chunk
 * resides in a 64KB contiguous slab allocated by ChunkPool, aligned to CPU
 * cache lines (64 bytes).
 *
 * ### Physical Memory Layout:
 * ```
 * +-----------------------+--------------------------+-------------------------+
 * | ChunkHeader (64B)     | Entity Array Column      | Component Columns...   |
 * | (count, capacity, etc)| [Entity 0, Entity 1,...] | [CompA 0...][CompB 0...|
 * +-----------------------+--------------------------+-------------------------+
 * ```
 *
 * All columns are stored contiguously in Structure-of-Arrays (SoA) format at
 * precomputed byte offsets calculated by Archetype. Component deletions use
 * swap-and-pop to guarantee O(1) removal and dense packing without holes.
 */
struct Chunk {
private:
  ChunkHeader header_;

  /**
   * @brief Private constructor initializing header capacity and zero-count
   * metadata.
   * @param capacity Maximum number of entities this chunk can store.
   * @param chunk_size Total chunk size in bytes (unused, defaults to 0).
   */
  explicit Chunk(uint32_t capacity, uint32_t chunk_size = 0) noexcept;

  ~Chunk() = default;

public:
  Chunk(const Chunk &) = delete;
  Chunk &operator=(const Chunk &) = delete;
  Chunk(Chunk &&) = delete;
  Chunk &operator=(Chunk &&) = delete;

  // --- Creation & Lifecycle --------------------------------------------------

  /**
   * @brief Placement-news a Chunk instance directly into an aligned raw memory
   * buffer.
   * @param buffer Raw pointer to the preallocated 64KB aligned memory page.
   * @param capacity Maximum entity capacity calculated for the owning
   * archetype.
   * @param chunk_size Total page size in bytes (optional, default: 0).
   * @return Pointer to the newly initialized Chunk header.
   */
  [[nodiscard]] static Chunk *create(
      void *buffer,
      uint32_t capacity,
      uint32_t chunk_size = 0
  ) noexcept;

  // --- Header & Metadata Introspection ---------------------------------------

  /// Returns a mutable reference to the internal ChunkHeader.
  [[nodiscard]] ChunkHeader &header() noexcept;

  /// Returns a const reference to the internal ChunkHeader.
  [[nodiscard]] const ChunkHeader &header() const noexcept;

  /// Returns the number of currently active entities in this chunk.
  [[nodiscard]] uint32_t count() const noexcept;

  /// Returns the maximum entity capacity of this chunk.
  [[nodiscard]] uint32_t capacity() const noexcept;

  /// Returns the zero-based index of this chunk within its archetype storage
  /// array.
  [[nodiscard]] uint32_t chunk_index() const noexcept;

  /// Returns true if the chunk has reached its maximum entity capacity.
  [[nodiscard]] bool is_full() const noexcept;

  /// Returns true if the chunk contains zero active entities.
  [[nodiscard]] bool is_empty() const noexcept;

  /// Returns a pointer to the next chunk header in the intrusive linked chain.
  [[nodiscard]] ChunkHeader *next() const noexcept;

  /// Sets the zero-based index of this chunk within its owning archetype.
  void set_chunk_index(uint32_t idx) noexcept;

  /// Sets the next chunk header pointer in the intrusive linked chain.
  void set_next(ChunkHeader *n) noexcept;

  // --- Column Slices & Data Pointers -----------------------------------------

  /// Returns a mutable pointer to the contiguous Entity array column.
  [[nodiscard]] Entity *entity_slice() noexcept;

  /// Returns a const pointer to the contiguous Entity array column.
  [[nodiscard]] const Entity *entity_slice() const noexcept;

  /**
   * @brief Obtains a typed pointer to a component column starting at a byte
   * offset.
   * @tparam T Component type.
   * @param byte_offset Chunk-relative byte offset of the component column.
   * @return Typed pointer to the first component element in the column.
   */
  template <typename T>
  [[nodiscard]] T *typed_slice(uint32_t byte_offset) noexcept;

  /**
   * @brief Obtains a const typed pointer to a component column starting at a
   * byte offset.
   * @tparam T Component type.
   * @param byte_offset Chunk-relative byte offset of the component column.
   * @return Const typed pointer to the first component element in the column.
   */
  template <typename T>
  [[nodiscard]] const T *typed_slice(uint32_t byte_offset) const noexcept;

  /**
   * @brief Accesses chunk-local user metadata storage.
   * @tparam T Trivially copyable POD metadata type.
   * @return Mutable reference to metadata.
   */
  template <typename T> [[nodiscard]] T &user_metadata() noexcept;

  /**
   * @brief Accesses chunk-local user metadata storage (const).
   * @tparam T Trivially copyable POD metadata type.
   * @return Const reference to metadata.
   */
  template <typename T> [[nodiscard]] const T &user_metadata() const noexcept;

  // --- Change Detection & Versioning -----------------------------------------

  /**
   * @brief Retrieves the change detection tick version for a specific component
   * column index.
   * @param col Zero-based column index.
   * @return Tick version when this column was last mutated.
   */
  [[nodiscard]] uint32_t column_version(uint16_t col) const noexcept;

  /**
   * @brief Updates the change detection tick version for a specific component
   * column index.
   * @param col Zero-based column index.
   * @param version Monotonically advancing simulation version tick.
   */
  void set_column_version(uint16_t col, uint32_t version) noexcept;

  /// Retrieves the structural modification version of this chunk.
  [[nodiscard]] uint32_t structure_version() const noexcept;

  /// Sets the structural modification version of this chunk.
  void set_structure_version(uint32_t version) noexcept;

  // --- Component Population & Blitting ---------------------------------------

  /**
   * @brief Broadcast-fills a range of rows with a uniform component value.
   * @tparam Comp Component type.
   * @param layout Column layout descriptor defining byte offsets.
   * @param start_row First row index to populate.
   * @param count Number of consecutive rows to fill.
   * @param component Source component value to replicate.
   */
  template <typename Comp>
  void fill_column(
      const ComponentColumnLayout &layout,
      uint32_t start_row,
      uint32_t count,
      const Comp &component
  );

  /**
   * @brief Bulk copies an external contiguous array of component values into
   * this column.
   * @tparam Comp Component type.
   * @param layout Column layout descriptor defining byte offsets.
   * @param start_row Destination starting row index in this chunk.
   * @param src_data Pointer to contiguous external source component array.
   * @param count Number of elements to copy.
   */
  template <typename Comp>
  void copy_column(
      const ComponentColumnLayout &layout,
      uint32_t start_row,
      const Comp *src_data,
      uint32_t count
  );

  /**
   * @brief Retrieves a raw byte pointer to a column at a chunk-relative byte
   * offset.
   * @param chunk_byte_offset Offset in bytes from the chunk base address.
   * @return Untyped pointer to column memory.
   */
  [[nodiscard]] void *component_slice(uint32_t chunk_byte_offset) noexcept;

  /**
   * @brief Retrieves a const raw byte pointer to a column at a chunk-relative
   * byte offset.
   * @param chunk_byte_offset Offset in bytes from the chunk base address.
   * @return Const untyped pointer to column memory.
   */
  [[nodiscard]] const void *component_slice(
      uint32_t chunk_byte_offset
  ) const noexcept;

  /**
   * @brief Computes a direct pointer to a specific component element at row
   * index.
   * @param layout Column layout descriptor.
   * @param row Zero-based row index within this chunk.
   * @return Raw pointer to the target component instance.
   */
  [[nodiscard]] void *component_element(
      const ComponentColumnLayout &layout,
      uint32_t row
  ) noexcept;

  /**
   * @brief Computes a const pointer to a specific component element at row
   * index.
   * @param layout Column layout descriptor.
   * @param row Zero-based row index within this chunk.
   * @return Const raw pointer to the target component instance.
   */
  [[nodiscard]] const void *component_element(
      const ComponentColumnLayout &layout,
      uint32_t row
  ) const noexcept;

  // --- Entity Ingestion & Deletion -------------------------------------------

  /**
   * @brief Retrieves the entity handle residing at a specified row index.
   * @param row Target zero-based row index.
   * @return Entity handle.
   */
  [[nodiscard]] Entity entity(uint32_t row) const noexcept;

  /**
   * @brief Appends a single entity handle to the chunk and increments count.
   * @param entity Target entity handle to append.
   * @return Row index allocated to the entity.
   */
  uint32_t append_entity(Entity entity) noexcept;

  /**
   * @brief Bulk-appends a span of entity handles to the chunk and advances
   * count.
   * @param entities Contiguous span of entity handles to append.
   * @return Starting row index assigned to the first entity in the span.
   */
  uint32_t append_entities(std::span<const Entity> entities) noexcept;

  /**
   * @brief Removes an entity by swapping it with the last row and decrementing
   * count.
   *
   * Destructs components at `row`. If `row` is not the tail, relocates
   * components and entity handle from `count - 1` into `row`, returning the
   * moved entity handle for SlotMap cache update.
   *
   * @param row Row index of the entity to destroy.
   * @param layouts Active archetype column layouts describing component
   * positions and sizes.
   * @return ChunkRelocation record containing the moved entity handle (or null
   * if tail).
   */
  [[nodiscard]] ChunkRelocation swap_and_pop(
      uint32_t row,
      std::span<const ComponentColumnLayout> layouts
  );

  /**
   * @brief Performs batch swap-and-pop deletion for multiple entity rows in a
   * single pass.
   *
   * Minimizes relocations by processing deleted rows in descending order and
   * filling holes exclusively from non-deleted tail rows.
   *
   * @param rows_to_delete Row indices to remove (must be valid within chunk
   * count).
   * @param layouts Archetype component column descriptors.
   * @param out_relocations Destination buffer receiving relocation records for
   * SlotMap updates.
   * @return Number of entity relocations recorded in `out_relocations`.
   */
  uint32_t batch_swap_and_pop(
      std::span<const uint32_t> rows_to_delete,
      std::span<const ComponentColumnLayout> layouts,
      std::span<EntityRelocation> out_relocations
  );
};

} // namespace arche
