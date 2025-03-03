/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANALYTICAL_ENGINE_CORE_FRAGMENT_ARROW_PROJECTED_FRAGMENT_MAPPER_H_
#define ANALYTICAL_ENGINE_CORE_FRAGMENT_ARROW_PROJECTED_FRAGMENT_MAPPER_H_

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include "arrow/array.h"
#include "boost/lexical_cast.hpp"

#include "grape/fragment/fragment_base.h"
#include "vineyard/basic/ds/arrow_utils.h"
#include "vineyard/common/util/version.h"
#include "vineyard/graph/fragment/property_graph_types.h"

#include "core/fragment/arrow_projected_fragment.h"

namespace gs {

/**
 * @brief Create a new arrowProjectedFragment with new vdata and new edata.
 *
 * @tparam OID_T OID type
 * @tparam VID_T VID type
 */
template <typename OID_T, typename VID_T, typename OLD_VDATA_T,
          typename NEW_VDATA_T, typename OLD_EDATA_T, typename NEW_EDATA_T>
class ArrowProjectedFragmentMapper {
 public:
  using label_id_t = vineyard::property_graph_types::LABEL_ID_TYPE;
  using prop_id_t = vineyard::property_graph_types::PROP_ID_TYPE;
  using vid_t = VID_T;
  using oid_t = OID_T;
  using old_vdata_t = OLD_VDATA_T;
  using new_vdata_t = NEW_VDATA_T;
  using old_edata_t = OLD_EDATA_T;
  using new_edata_t = NEW_EDATA_T;

  using edata_array_builder_t =
      typename vineyard::ConvertToArrowType<new_edata_t>::BuilderType;
  using edata_array_t =
      typename vineyard::ConvertToArrowType<new_edata_t>::ArrayType;
  using vineyard_edata_array_builder_t =
      typename vineyard::InternalType<new_edata_t>::vineyard_builder_type;

  using vdata_array_builder_t =
      typename vineyard::ConvertToArrowType<new_vdata_t>::BuilderType;
  using vdata_array_t =
      typename vineyard::ConvertToArrowType<new_vdata_t>::ArrayType;
  using vineyard_vdata_array_builder_t =
      typename vineyard::InternalType<new_vdata_t>::vineyard_builder_type;

  using old_frag_t =
      ArrowProjectedFragment<oid_t, vid_t, old_vdata_t, old_edata_t>;
  using new_frag_t =
      ArrowProjectedFragment<oid_t, vid_t, new_vdata_t, new_edata_t>;

  ArrowProjectedFragmentMapper() {}
  ~ArrowProjectedFragmentMapper() {}

  std::shared_ptr<new_frag_t> Map(old_frag_t& old_fragment,
                                  vdata_array_builder_t& vdata_array_builder,
                                  edata_array_builder_t& edata_array_builder,
                                  vineyard::Client& client) {
    const vineyard::ObjectMeta& old_meta = old_fragment.meta();
    std::shared_ptr<vineyard::ArrowFragment<oid_t, vid_t>> old_arrow_fragment =
        old_fragment.fragment_;
    auto v_label = old_meta.GetKeyValue<int>("projected_v_label");
    auto e_label = old_meta.GetKeyValue<int>("projected_e_label");
    auto old_vprop_num = old_arrow_fragment->vertex_property_num(v_label);
    auto old_eprop_num = old_arrow_fragment->edge_property_num(e_label);
    LOG(INFO) << "Old arrow fragment has " << old_vprop_num
              << " vertex properties and " << old_eprop_num
              << " edge properties";
    auto new_vprop_name = "VPROP_" + std::to_string(old_vprop_num);
    auto new_eprop_name = "EPROP_" + std::to_string(old_eprop_num);

    std::shared_ptr<edata_array_t> arrow_edata_array;
    edata_array_builder.Finish(&arrow_edata_array);
    std::shared_ptr<vdata_array_t> arrow_vdata_array;
    vdata_array_builder.Finish(&arrow_vdata_array);
    std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>
        vertex_columns_map;
    std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>
        edge_columns_map;
    vertex_columns_map.push_back(
        std::make_pair(new_vprop_name, arrow_vdata_array));
    edge_columns_map.push_back(
        std::make_pair(new_eprop_name, arrow_edata_array));

    vineyard::ObjectID new_frag_id;

    // Add new data to original ArrowFragment's table.
    {
      std::map<
          label_id_t,
          std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>>
          v_map;
      v_map[v_label] = vertex_columns_map;
      vineyard::ObjectID new_arrow_fragment_id = bl::try_handle_all(
          [&]() { return old_arrow_fragment->AddVertexColumns(client, v_map); },
          [](const vineyard::GSError& e) {
            LOG(ERROR) << e.error_msg;
            return 0;
          },
          [](const bl::error_info& unmatched) {
            LOG(ERROR) << "Unmatched error " << unmatched;
            return 0;
          });
      LOG(INFO) << "Build First fragment: " << new_arrow_fragment_id;
      std::shared_ptr<vineyard::ArrowFragment<oid_t, vid_t>>
          new_arrow_fragment =
              std::dynamic_pointer_cast<vineyard::ArrowFragment<oid_t, vid_t>>(
                  client.GetObject(new_arrow_fragment_id));

      std::map<
          label_id_t,
          std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>>
          e_map;
      e_map[e_label] = edge_columns_map;
      vineyard::ObjectID new_new_arrow_fragment_id = bl::try_handle_all(
          [&]() { return new_arrow_fragment->AddEdgeColumns(client, e_map); },
          [](const vineyard::GSError& e) {
            LOG(ERROR) << e.error_msg;
            return 0;
          },
          [](const bl::error_info& unmatched) {
            LOG(ERROR) << "Unmatched error " << unmatched;
            return 0;
          });

      LOG(INFO) << "Build Second fragment: " << new_new_arrow_fragment_id;
      std::shared_ptr<vineyard::ArrowFragment<oid_t, vid_t>>
          new_new_arrow_fragment =
              std::dynamic_pointer_cast<vineyard::ArrowFragment<oid_t, vid_t>>(
                  client.GetObject(new_new_arrow_fragment_id));
      auto schema = new_new_arrow_fragment->schema();
      auto v_prop_id = schema.GetVertexPropertyId(v_label, new_vprop_name);
      auto e_prop_id = schema.GetEdgePropertyId(e_label, new_eprop_name);
      std::shared_ptr<new_frag_t> res = new_frag_t::Project(
          new_new_arrow_fragment, 0, v_prop_id, 0, e_prop_id);
      new_frag_id = res->id();
    }

    LOG(INFO) << "Got projected fragment: " << new_frag_id;
    auto new_frag =
        std::dynamic_pointer_cast<new_frag_t>(client.GetObject(new_frag_id));
    return new_frag;
  }
};
}  // namespace gs

#endif  // ANALYTICAL_ENGINE_CORE_FRAGMENT_ARROW_PROJECTED_FRAGMENT_MAPPER_H_
