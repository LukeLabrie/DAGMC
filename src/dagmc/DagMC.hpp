#ifndef MOABMC_HPP
#define MOABMC_HPP

#include <assert.h>

#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "DagMCVersion.hpp"
#include "MBTagConventions.hpp"
#include "logger.hpp"
#include "moab/CartVect.hpp"
#include "moab/Core.hpp"
#include "moab/FileOptions.hpp"
#include "moab/GeomQueryTool.hpp"
#include "moab/GeomTopoTool.hpp"
#include "moab/GeomUtil.hpp"
#include "moab/Interface.hpp"
#include "moab/Range.hpp"

class RefEntity;

struct DagmcVolData {
  int mat_id;
  double density, importance;
  std::string comp_name;
};

namespace double_down {
class RayTracingInterface;
}

namespace moab {

static const int vertex_handle_idx = 0;
static const int curve_handle_idx = 1;
static const int surfs_handle_idx = 2;
static const int vols_handle_idx = 3;
static const int groups_handle_idx = 4;
static const std::string GRAVEYARD_NAME = "mat:graveyard";

class CartVect;
class GeomQueryTool;

/**\brief
 *
 * In section 1, the public interface you will find all the functions needed
 * for problem setup. For the typical MC code, the order of function calls
 * required to fully populate DAGMC ready to run are
 *
 *    1) DAG->load_file();
 *    2) DAG->init_OBBTree();
 *
 * Modifications were made to init_OBBTree which allows the functions of
 * init_OBBTree to be called without having used init_OBBTree. For example
 * if you would like access to be able to call DAG->point_in_volume() but
 * without having an implicit compliment you need only call
 *
 *   1) DAG->load_file();
 *   2) DAG->setup_obb();
 *
 * Similarly, if you need access to problem indices only, one may call
 * load_file followed by setup_indices.
 *
 *   1) DAG->load_file();
 *   2) DAG->setup_indices();
 */

class DagMC {
 public:
  // Constructor
  DagMC(std::shared_ptr<Interface> mb_impl = nullptr,
        double overlap_tolerance = 0., double numerical_precision = .001,
        int verbosity = 1);
  // Deprecated Constructor
  [[deprecated(
      "Replaced by DagMC(std::shared_ptr<Interface> mb_impl, ... "
      ")")]] DagMC(Interface* mb_impl, double overlap_tolerance = 0.,
                   double numerical_precision = .001, int verbosity = 1);
  // Destructor
  ~DagMC();

  /** Return the version of this library */
  static float version(std::string* version_string = NULL);
  /** Get subversion revision of this file (DagMC.hpp) */
  [[deprecated]] static unsigned int interface_revision() { return 0; }

  /** Git revision of DAGMC */
  inline std::string git_sha() { return DAGMC_GIT_SHA; }

  /* SECTION I: Geometry Initialization */

  /**\brief Load a geometry description regardless of format
   *
   * This method will load the geometry file with name cfile.
   * In case this is a solid model geometry file, it will pass
   * the facet_tolerance option as guidance for the faceting engine.
   *\param cfile the file name to be loaded
   *\param facet_tolerance the faceting tolerance guidance for the faceting
   *engine \return - MB_SUCCESS if file loads correctly
   *        - other MB ErrorCodes returned from MOAB
   *
   * Note: When loading a prexisting file with an OBB_TREE tag, a number of
   *unspoken things happen that one should be aware of.
   *
   * 1) The file is loaded and when we query the meshset, we find entities with
   *the OBB_TREE tag 2) The OBBTreeTool assumes that any children of the entity
   *being queried in a ray intersect sets operation are fair game, the surface
   *meshsets have triangles as members, but OBBs as children but no querying is
   *done, just assumptions that the tags exist.
   */
  ErrorCode load_file(const char* cfile);

  /*\brief Use pre-loaded geometry set
   *
   * Works like load_file, but using data that has been externally
   * loaded into DagMC's MOAB instance.
   * Only one of the two functions should be called.
   *
   * TODO: this function should accept a parameter, being the
   * entity set to use for DagMC's data.  Currently DagMC always
   * assumes that all the contents of its MOAB instance belong to it.
   */
  ErrorCode load_existing_contents();

  /**\brief initializes the geometry and OBB tree structure for ray firing
   * acceleration
   *
   * This method can be called after load_file to fully initialize DAGMC.  It
   * calls methods to set up the geometry, create the implicit complement,
   * generate an OBB tree from the faceted representation of the geometry, and
   * build the cross-referencing indices.
   */
  ErrorCode init_OBBTree();

  /**\brief finds or creates the implicit complement
   *
   * This method calls the GeomTopoTool->get_implicit_complement which will
   * return the IC if it already exists. If the IC doesn't exist, it will
   * create one.
   */
  ErrorCode setup_impl_compl();

  /**\brief sets up ranges of the volume and surface entity sets
   *
   * Helper function for setup_indices. Sets ranges containing
   * all volumes and surfaces.
   */
  ErrorCode setup_geometry(Range& surfs, Range& vols);

  /**\brief constructs obb trees for all surfaces and volumes
   *
   * Very thin wrapper around GTT->construct_obb_trees().
   * Constructs obb trees for all surfaces and volumes in the geometry.
   */
  ErrorCode setup_obbs();

  /**\brief thin wrapper around build_indices()
   *
   * Very thin wrapper around build_indices().
   */
  ErrorCode setup_indices();

  /**\brief Removes the graveyard if one is present. */
  ErrorCode remove_graveyard();

  /**\brief Create a graveyard (a volume representing the volume boundary).
   *
   * Create a cuboid volume marked with metadata indicating it is the boundary
   * of the DAGMC model. This method will fail if a graveyard volume already
   * exists and `overwrite` is not true. Requires BVH tree's existence.
   *
   */
  ErrorCode create_graveyard(bool overwrite = false);

  /** Returns true if the model has a graveyard volume, false if not */
  bool has_graveyard();

  /** Returns true if the model has any trees, false if not */
  bool has_acceleration_datastructures();

  /** Retrieve the graveyard group on the model if it exists */
  ErrorCode get_graveyard_group(EntityHandle& graveyard_group);

 private:
  /** convenience function for converting a bounding box into a box of triangles
   *  with outward facing normals and setting up set structure necessary for
   *  representation as a geometric entity
   */
  ErrorCode box_to_surf(const double llc[3], const double urc[3],
                        EntityHandle& surface_set);

  /**\brief Removes the BVH for the specified volume */
  ErrorCode remove_bvh(EntityHandle volume, bool unjoin_vol = false);

  /**\brief Builds the BVH for a specified volume */
  ErrorCode build_bvh(EntityHandle volume);

  /** loading code shared by load_file and load_existing_contents */
  ErrorCode finish_loading();

  /* SECTION II: Fundamental Geometry Operations/Queries */
 public:
  /** The methods in this section are thin wrappers around methods in the
   *  GeometryQueryTool.
   */

  typedef GeomQueryTool::RayHistory RayHistory;

  ErrorCode ray_fire(const EntityHandle volume, const double ray_start[3],
                     const double ray_dir[3], EntityHandle& next_surf,
                     double& next_surf_dist, RayHistory* history = NULL,
                     double dist_limit = 0, int ray_orientation = 1,
                     OrientedBoxTreeTool::TrvStats* stats = NULL);

  ErrorCode point_in_volume(const EntityHandle volume, const double xyz[3],
                            int& result, const double* uvw = NULL,
                            const RayHistory* history = NULL);

  ErrorCode point_in_volume_slow(const EntityHandle volume, const double xyz[3],
                                 int& result);
#if MOAB_VERSION_MAJOR == 5 && MOAB_VERSION_MINOR > 2
  ErrorCode find_volume(const double xyz[3], EntityHandle& volume,
                        const double* uvw = NULL);
#endif

  ErrorCode test_volume_boundary(const EntityHandle volume,
                                 const EntityHandle surface,
                                 const double xyz[3], const double uvw[3],
                                 int& result, const RayHistory* history = NULL);

  ErrorCode closest_to_location(EntityHandle volume, const double point[3],
                                double& result, EntityHandle* surface = 0);

  ErrorCode measure_volume(EntityHandle volume, double& result);

  ErrorCode measure_area(EntityHandle surface, double& result);

  ErrorCode surface_sense(EntityHandle volume, int num_surfaces,
                          const EntityHandle* surfaces, int* senses_out);

  ErrorCode surface_sense(EntityHandle volume, EntityHandle surface,
                          int& sense_out);

  ErrorCode get_angle(EntityHandle surf, const double xyz[3], double angle[3],
                      const RayHistory* history = NULL);

  ErrorCode next_vol(EntityHandle surface, EntityHandle old_volume,
                     EntityHandle& new_volume);

  /* SECTION III: Indexing & Cross-referencing */
 public:
  /** Most calling apps refer to geometric entities with a combination of
   *  base-1/0 ordinal index (or rank) and global ID (or name).
   *  DagMC also has an internal EntityHandle reference to each geometric
   * entity. These method provide ways to translate from one to the other.
   */

  /** map from dimension & global ID to EntityHandle */
  EntityHandle entity_by_id(int dimension, int id) const;
  /** map from dimension & base-1 ordinal index to EntityHandle */
  EntityHandle entity_by_index(int dimension, int index) const;
  /** map from dimension & base-1 ordinal index to global ID */
  int id_by_index(int dimension, int index) const;
  /** PPHW: Missing dim & global ID ==> base-1 ordinal index */
  /** map from EntityHandle to base-1 ordinal index */
  int index_by_handle(EntityHandle handle) const;
  /** map from EntityHandle to global ID */
  int get_entity_id(EntityHandle this_ent) const;

  /**\brief get number of geometric sets corresponding to geometry of specified
   *dimension
   *
   * For a given dimension (e.g. dimension=3 for volumes, dimension=2 for
   *surfaces) return the number of entities of that dimension \param dimension
   *the dimensionality of the entities in question \return integer number of
   *entities of that dimension
   */
  unsigned int num_entities(int dimension) const;

 private:
  /** get all group sets on the model */
  ErrorCode get_groups(Range& groups);

  /** build internal index vectors that speed up handle-by-id, etc. */
  ErrorCode build_indices(Range& surfs, Range& vols);

  /* SECTION IV: Handling DagMC settings */
 public:
  /** retrieve overlap thickness */
  double overlap_thickness();
  /** retrieve numerical precision */
  double numerical_precision();
  /** retrieve faceting tolerance */
  double faceting_tolerance() { return facetingTolerance; }

  /** Attempt to set a new overlap thickness tolerance, first checking for
   * sanity */
  void set_overlap_thickness(double new_overlap_thickness);

  /** Attempt to set a new numerical precision , first checking for sanity
   *  Use of this function is discouraged; see top of DagMC.cpp
   */
  void set_numerical_precision(double new_precision);

  /* SECTION V: Metadata handling */
  /** Detect all the property keywords that appear in the loaded geometry
   *
   *  @param keywords_out The result list of keywords.  This list could be
   *        validly passed to parse_properties().
   */
  ErrorCode detect_available_props(std::vector<std::string>& keywords_out,
                                   const char* delimiters = "_");

  /** Parse properties from group names per metadata syntax standard
   *
   *  @param keywords A list of keywords to parse.  These are considered the
   * canonical names of the properties, and constitute the valid inputs to
   *                  has_prop() and prop_value().
   *  @param delimiters An array of characters the routine will use to split the
   * groupname into properties.
   *  @param synonyms An optional mapping of synonym keywords to canonical
   * keywords. This allows more than one group name keyword to take on the same
   *                  meaning
   *                  e.g. if synonyms["rest.of.world"] = "graveyard", then
   * volumes in the "rest.of.world" group will behave as if they were in a group
   * named "graveyard".
   */
  ErrorCode parse_properties(
      const std::vector<std::string>& keywords,
      const std::map<std::string, std::string>& synonyms = no_synonyms,
      const char* delimiters = "_");

  /** Get the value of a property on a volume or surface
   *
   *  @param eh The entity handle to get a property value on
   *  @param prop The canonical property name
   *  @param value Output parameter, the value of the property.  If no value was
   *               set on the handle, this will be the empty string.
   *  @return MB_TAG_NOT_FOUND if prop is invalid.  Otherwise return any errors
   * from MOAB, or MB_SUCCESS if successful
   */
  ErrorCode prop_value(EntityHandle eh, const std::string& prop,
                       std::string& value);

  /** Get the value of a property on a volume or surface
   *
   *  @param eh The entity handle to get a property value on
   *  @param prop The canonical property name
   *  @param values Output parameter, the values of the property will be
   * appended to this list.  If no value was set on the handle, no entries will
   * be added.
   *  @return MB_TAG_NOT_FOUND if prop is invalid.  Otherwise return any errors
   * from MOAB, or MB_SUCCESS if successful
   */
  ErrorCode prop_values(EntityHandle eh, const std::string& prop,
                        std::vector<std::string>& value);

  /** Return true if a volume or surface has the named property set upon it
   *
   *  @param eh The entity handle to query
   *  @param prop The canonical property name
   *  @retrun True if the handle has the property set, or false if not.
   *          False is also returned if a MOAB error occurs.
   */
  bool has_prop(EntityHandle eh, const std::string& prop);

  /** Get a list of all unique values assigned to a named property on any entity
   *
   *  @param prop The canonical property name
   *  @param return_list Output param, a list of unique strings that are set as
   * values for this property
   *  @return MB_TAG_NOT_FOUND if prop is invalid.  Otherwise return any errors
   * from MOAB, or MB_SUCCESS if succesful
   */
  ErrorCode get_all_prop_values(const std::string& prop,
                                std::vector<std::string>& return_list);

  /** Get a list of all entities which have a given property
   *
   *  @param prop The canonical property name
   *  @param return_list Output param, a list of entity handles that have this
   * property
   *  @param dimension If nonzero, entities returned will be restricted to the
   * given dimension, i.e. 2 for surfaces and 3 for volumes
   *  @parm value If non-NULL, only entities for which the property takes on
   * this value will be returned.
   *  @return MB_TAG_NOT_FOUND if prop is invalid.  Otherwise return any errors
   * from MOAB, or MB_SUCCESS if successful
   */
  ErrorCode entities_by_property(const std::string& prop,
                                 std::vector<EntityHandle>& return_list,
                                 int dimension = 0,
                                 const std::string* value = NULL);

  bool is_implicit_complement(EntityHandle volume);

  /** get the tag for the "name" of a surface == global ID */
  Tag name_tag() { return nameTag; }

  /** Get the tag used to associate OBB trees with geometry in load_file(..).
   * not sure what to do about the obb_tag, GTT has no concept of an obb_tag on
   * EntitySets - PCS
   */
  Tag obb_tag() { return NULL; }
  Tag category_tag();
  Tag geom_tag() { return GTT->get_geom_tag(); }
  Tag id_tag() { return GTT->get_gid_tag(); }
  Tag sense_tag() { return GTT->get_sense_tag(); }

 private:
  /** tokenize the metadata stored in group names - basically borrowed from
   * ReadCGM.cpp */
  void tokenize(const std::string& str, std::vector<std::string>& tokens,
                const char* delimiters = "_") const;

  /** a common type within the property and group name functions */
  typedef std::map<std::string, std::string> prop_map;

  /** Store the name of a group in a string */
  ErrorCode get_group_name(EntityHandle group_set, std::string& name);
  /** Parse a group name into a set of key:value pairs */
  ErrorCode parse_group_name(EntityHandle group_set, prop_map& result,
                             const char* delimiters = "_");
  /** Add a string value to a property tag for a given entity */
  ErrorCode append_packed_string(Tag, EntityHandle, std::string&);
  /** Convert a property tag's value on a handle to a list of strings */
  ErrorCode unpack_packed_string(Tag tag, EntityHandle eh,
                                 std::vector<std::string>& values);

  std::vector<EntityHandle>& surf_handles() {
    return entHandles[surfs_handle_idx];
  }
  std::vector<EntityHandle>& vol_handles() {
    return entHandles[vols_handle_idx];
  }
  std::vector<EntityHandle>& group_handles() {
    return entHandles[groups_handle_idx];
  }

  Tag get_tag(const char* name, int size, TagType store, DataType type,
              const void* def_value = NULL, bool create_if_missing = true);

  /* SECTION VI: Other */
 public:
  OrientedBoxTreeTool* obb_tree() { return GTT->obb_tree(); }

  std::shared_ptr<GeomTopoTool> geom_tool() { return GTT; }

  ErrorCode write_mesh(const char* ffile, const int flen);

  /** get the corners of the OBB for a given volume */
  ErrorCode getobb(EntityHandle volume, double minPt[3], double maxPt[3]);

  /** get the center point and three vectors for the OBB of a given volume */
  ErrorCode getobb(EntityHandle volume, double center[3], double axis1[3],
                   double axis2[3], double axis3[3]);

  /** get the root of the obbtree for a given entity */
  ErrorCode get_root(EntityHandle vol_or_surf, EntityHandle& root);

  /** Get the instance of MOAB used by functions in this file. */
  Interface* moab_instance() { return MBI; }
  std::shared_ptr<Interface> moab_instance_sptr() {
    if (nullptr == MBI_shared_ptr)
      std::runtime_error("MBI instance is not defined as a shared pointer !");
    return MBI_shared_ptr;
  }

 private:
  /* PRIVATE MEMBER DATA */

  // Shared_ptr owning *MBI (if allocated internally)
  std::shared_ptr<Interface> MBI_shared_ptr;
  // Use for the call to MOAB interface, should never be deleted in the DagMC
  // instanced MBI is either externally owned or owned by the MBI_shared_ptr
  Interface* MBI;
  bool moab_instance_created;

  std::shared_ptr<GeomTopoTool> GTT;
  // type alias for ray tracing engine
#ifdef DOUBLE_DOWN
  using RayTracer = double_down::RayTracingInterface;
#else
  using RayTracer = GeomQueryTool;
#endif

  std::unique_ptr<RayTracer> ray_tracer;

 public:
  Tag nameTag, facetingTolTag;

 private:
  /** store some lists indexed by handle */
  std::vector<EntityHandle> entHandles[5];
  /** surface and volume mapping from EntitiyHandle to DAGMC index */
  std::unordered_map<EntityHandle, int> entIndices;
  /** corresponding geometric entities; also indexed like rootSets */
  std::vector<RefEntity*> geomEntities;

  /* metadata */
  /** empty synonym map to provide as a default argument to parse_properties()
   */
  static const std::map<std::string, std::string> no_synonyms;
  /** map from the canonical property names to the tags representing them */
  std::map<std::string, Tag> property_tagmap;

  char implComplName[NAME_TAG_SIZE];

  double facetingTolerance;

  /** vectors for point_in_volume: */
  std::vector<double> disList;
  std::vector<int> dirList;
  std::vector<EntityHandle> surList, facList;

  /** logger **/
  DagMC_Logger logger;

  // axis-aligned box used to track geometry bounds
  // (internal use only)
  struct BBOX {
    constexpr static double INFTY{std::numeric_limits<double>::max()};

    double lower[3] = {INFTY, INFTY, INFTY};
    double upper[3] = {-INFTY, -INFTY, -INFTY};

    /** ensure box corners are valid */
    bool valid() {
      return (lower[0] <= upper[0] && lower[1] <= upper[1] &&
              lower[2] <= upper[2]);
    }

    /** update box to ensure the provided point is contained */
    void update(double x, double y, double z) {
      lower[0] = x < lower[0] ? x : lower[0];
      lower[1] = y < lower[1] ? y : lower[1];
      lower[2] = z < lower[2] ? z : lower[2];

      upper[0] = x > upper[0] ? x : upper[0];
      upper[1] = y > upper[1] ? y : upper[1];
      upper[2] = z > upper[2] ? z : upper[2];
    }

    /** expand the box by some absolute value*/
    void expand(double bump) {
      for (int i = 0; i < 3; i++) {
        upper[i] += bump;
        lower[i] -= bump;
      }
    }

    /** update box to ensure the provided point is contained */
    void update(double xyz[3]) { update(xyz[0], xyz[1], xyz[2]); }
  };

};  // end DagMC

inline EntityHandle DagMC::entity_by_index(int dimension, int index) const {
  assert(2 <= dimension && 3 >= dimension &&
         (unsigned)index < entHandles[dimension].size());
  return entHandles[dimension][index];
}

inline int DagMC::index_by_handle(EntityHandle handle) const {
  assert(entIndices.count(handle) > 0);
  return entIndices.at(handle);
}

inline unsigned int DagMC::num_entities(int dimension) const {
  assert(vertex_handle_idx <= dimension && groups_handle_idx >= dimension);
  return entHandles[dimension].size() - 1;
}

inline ErrorCode DagMC::get_root(EntityHandle vol_or_surf, EntityHandle& root) {
  ErrorCode rval = GTT->get_root(vol_or_surf, root);
  MB_CHK_SET_ERR(rval, "Failed to get obb root set of volume or surface");
  return MB_SUCCESS;
}

}  // namespace moab

#endif
