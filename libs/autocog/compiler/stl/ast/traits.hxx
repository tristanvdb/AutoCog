#ifndef AUTOCOG_COMPILER_STL_AST_TRAITS_HXX
#define AUTOCOG_COMPILER_STL_AST_TRAITS_HXX

namespace autocog::compiler::stl::ast {

// Forward declaration
enum class Tag;
template<Tag> struct Node;

// ============================================================================
// Type aliases for all node container types
// ============================================================================

// Basic node type
template<Tag tag>
using node_t = Node<tag>;

// Unique pointer to node
template<Tag tag>
using pnode_t = std::unique_ptr<Node<tag>>;

// Optional node
template<Tag tag>
using onode_t = std::optional<Node<tag>>;

// List of nodes
template<Tag tag>
using nodes_t = std::list<Node<tag>>;

// List of unique pointers to nodes
template<Tag tag>
using pnodes_t = std::list<std::unique_ptr<Node<tag>>>;

// Map of string to nodes
template<Tag tag>
using mapped_t = std::unordered_map<std::string, Node<tag>>;

// Variant of nodes (variadic)
template<Tag... tags>
using variant_t = std::variant<Node<tags>...>;

// List of variant nodes
template<Tag... tags>
using variants_t = std::list<std::variant<Node<tags>...>>;

// ============================================================================
// Type traits for detecting node container types
// ============================================================================

// Trait for basic node
template<typename T>
struct is_node : std::false_type {};

template<Tag tag>
struct is_node<node_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_node_v = is_node<T>::value;

// Trait for unique pointer to node
template<typename T>
struct is_pnode : std::false_type {};

template<Tag tag>
struct is_pnode<pnode_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_pnode_v = is_pnode<T>::value;

// Trait for optional node
template<typename T>
struct is_onode : std::false_type {};

template<Tag tag>
struct is_onode<onode_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_onode_v = is_onode<T>::value;

// Trait for list of nodes
template<typename T>
struct is_nodes : std::false_type {};

template<Tag tag>
struct is_nodes<nodes_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_nodes_v = is_nodes<T>::value;

// Trait for list of unique pointers
template<typename T>
struct is_pnodes : std::false_type {};

template<Tag tag>
struct is_pnodes<pnodes_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_pnodes_v = is_pnodes<T>::value;

// Trait for mapped nodes
template<typename T>
struct is_mapped : std::false_type {};

template<Tag tag>
struct is_mapped<mapped_t<tag>> : std::true_type {};

template<typename T>
inline constexpr bool is_mapped_v = is_mapped<T>::value;

// Trait for variant nodes
template<typename T>
struct is_variant : std::false_type {};

template<Tag... tags>
struct is_variant<variant_t<tags...>> : std::true_type {};

template<typename T>
inline constexpr bool is_variant_v = is_variant<T>::value;

// Trait for list of variants
template<typename T>
struct is_variants : std::false_type {};

template<Tag... tags>
struct is_variants<variants_t<tags...>> : std::true_type {};

template<typename T>
inline constexpr bool is_variants_v = is_variants<T>::value;

// ============================================================================
// Utility traits
// ============================================================================

// Combined trait to check if type is any kind of node container
template<typename T>
inline constexpr bool is_any_node_container_v = 
    is_node_v<T> || 
    is_pnode_v<T> || 
    is_onode_v<T> || 
    is_nodes_v<T> || 
    is_pnodes_v<T> || 
    is_mapped_v<T> ||
    is_variant_v<T> || 
    is_variants_v<T>;

// Trait to extract the Tag from a single-tag node container
template<typename T>
struct extract_tag {
    static constexpr bool has_single_tag = false;
};

template<Tag tag>
struct extract_tag<node_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

template<Tag tag>
struct extract_tag<pnode_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

template<Tag tag>
struct extract_tag<onode_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

template<Tag tag>
struct extract_tag<nodes_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

template<Tag tag>
struct extract_tag<pnodes_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

template<Tag tag>
struct extract_tag<mapped_t<tag>> {
    static constexpr bool has_single_tag = true;
    static constexpr Tag value = tag;
};

// Helper to check if type has a single tag
template<typename T>
inline constexpr bool has_single_tag_v = extract_tag<T>::has_single_tag;

// Helper to get the tag value (only valid if has_single_tag_v is true)
template<typename T>
inline constexpr Tag extract_tag_v = extract_tag<T>::value;

} // namespace autocog::compiler::stl::ast

#endif // AUTOCOG_COMPILER_STL_AST_TRAITS_HXX
