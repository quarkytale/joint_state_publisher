/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2011, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author Ioan Sucan */

#include "srdf/model.h"
#include <ros/console.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>

void srdf::Model::loadVirtualJoints(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* vj_xml = robot_xml->FirstChildElement("virtual_joint"); vj_xml; vj_xml = vj_xml->NextSiblingElement("virtual_joint"))
  {
    const char *jname = vj_xml->Attribute("name");
    const char *child = vj_xml->Attribute("child_link");
    const char *parent = vj_xml->Attribute("parent_frame");
    const char *type = vj_xml->Attribute("type");
    if (!jname)
    {
      ROS_ERROR("Name of virtual joint is not specified");
      continue;
    }
    if (!child)
    {
      ROS_ERROR("Child link of virtual joint is not specified");
      continue;
    }
    if (!urdf_model.getLink(boost::trim_copy(std::string(child))))
    {
      ROS_ERROR("Virtual joint does not attach to a link on the robot (link '%s' is not known)", child);
      continue;
    }
    if (!parent)
    {
      ROS_ERROR("Parent frame of virtual joint is not specified");
      continue;
    }
    if (!type)
    {
      ROS_ERROR("Type of virtual joint is not specified");
      continue;
    }
    VirtualJoint vj;
    vj.type_ = std::string(type); boost::trim(vj.type_);
    std::transform(vj.type_.begin(), vj.type_.end(), vj.type_.begin(), ::tolower);
    if (vj.type_ != "planar" && vj.type_ != "floating" && vj.type_ != "fixed")
    {
      ROS_ERROR("Unknown type of joint: '%s'. Assuming 'fixed' instead. Other known types are 'planar' and 'floating'.", type);
      vj.type_ = "fixed";
    }
    vj.name_ = std::string(jname); boost::trim(vj.name_);        
    vj.child_link_ = std::string(child); boost::trim(vj.child_link_);
    vj.parent_frame_ = std::string(parent); boost::trim(vj.parent_frame_);
    virtual_joints_.push_back(vj);
  }
}

void srdf::Model::loadGroups(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* group_xml = robot_xml->FirstChildElement("group"); group_xml; group_xml = group_xml->NextSiblingElement("group"))
  {
    const char *gname = group_xml->Attribute("name");
    if (!gname)
    {
      ROS_ERROR("Group name not specified");
      continue;
    }
    Group g;
    g.name_ = std::string(gname); boost::trim(g.name_);
    
    // get the links in the groups
    for (TiXmlElement* link_xml = group_xml->FirstChildElement("link"); link_xml; link_xml = link_xml->NextSiblingElement("link"))
    {
      const char *lname = link_xml->Attribute("name");
      if (!lname)
      {
        ROS_ERROR("Link name not specified");
        continue;
      }
      std::string lname_str = boost::trim_copy(std::string(lname));
      if (!urdf_model.getLink(lname_str))
      {
        ROS_ERROR("Link '%s' declared as part of group '%s' is not known to the URDF", lname, gname);
        continue;
      }
      g.links_.push_back(lname_str);
    }
    
    // get the joints in the groups
    for (TiXmlElement* joint_xml = group_xml->FirstChildElement("joint"); joint_xml; joint_xml = joint_xml->NextSiblingElement("joint"))
    {
      const char *jname = joint_xml->Attribute("name");
      if (!jname)
      {
        ROS_ERROR("Joint name not specified");
        continue;
      }
      std::string jname_str = boost::trim_copy(std::string(jname));
      if (!urdf_model.getJoint(jname_str))
      {
        bool missing = true;
        for (std::size_t k = 0 ; k < virtual_joints_.size() ; ++k)
          if (virtual_joints_[k].name_ == jname_str)
          {
            missing = false;
            break;
          }
        if (missing)
        {
          ROS_ERROR("Joint '%s' declared as part of group '%s' is not known to the URDF", jname, gname);
          continue;
        }
      }
      g.joints_.push_back(jname_str);
    }
    
    // get the chains in the groups
    for (TiXmlElement* chain_xml = group_xml->FirstChildElement("chain"); chain_xml; chain_xml = chain_xml->NextSiblingElement("chain"))
    {
      const char *base = chain_xml->Attribute("base_link");
      const char *tip = chain_xml->Attribute("tip_link");
      if (!base)
      {
        ROS_ERROR("Base link name not specified for chain");
        continue;
      }
      if (!tip)
      {
        ROS_ERROR("Tip link name not specified for chain");
        continue;
      }
      std::string base_str = boost::trim_copy(std::string(base));
      std::string tip_str = boost::trim_copy(std::string(tip));
      if (!urdf_model.getLink(base_str))
      {
        ROS_ERROR("Link '%s' declared as part of a chain in group '%s' is not known to the URDF", base, gname);
        continue;
      }
      if (!urdf_model.getLink(tip_str))
      {
        ROS_ERROR("Link '%s' declared as part of a chain in group '%s' is not known to the URDF", tip, gname);
        continue;
      }
      bool found = false;
      boost::shared_ptr<const urdf::Link> l = urdf_model.getLink(tip_str);
      std::set<std::string> seen;
      while (!found && l)
      {
        seen.insert(l->name);
        if (l->name == base_str)
          found = true;
        else
          l = l->getParent();
      }
      if (!found)
      {
        l = urdf_model.getLink(base_str);
        while (!found && l)
        {
          if (seen.find(l->name) != seen.end())
            found = true;
          else
            l = l->getParent();
        }
      }
      if (found)
        g.chains_.push_back(std::make_pair(base_str, tip_str));
      else
        ROS_ERROR("Links '%s' and '%s' do not form a chain. Not included in group '%s'", base, tip, gname);
    }
    
    // get the subgroups in the groups
    for (TiXmlElement* subg_xml = group_xml->FirstChildElement("group"); subg_xml; subg_xml = subg_xml->NextSiblingElement("group"))
    {
      const char *sub = subg_xml->Attribute("name");
      if (!sub)
      {
        ROS_ERROR("Group name not specified when included as subgroup");
        continue;
      }
      g.subgroups_.push_back(boost::trim_copy(std::string(sub)));
    }
    if (g.links_.empty() && g.joints_.empty() && g.chains_.empty() && g.subgroups_.empty())
      ROS_WARN("Group '%s' is empty.", gname);
    groups_.push_back(g);
  }
  
  // check the subgroups
  std::set<std::string> known_groups;
  bool update = true;
  while (update)
  {
    update = false;
    for (std::size_t i = 0 ; i < groups_.size() ; ++i)
    {
      if (known_groups.find(groups_[i].name_) != known_groups.end())
        continue;
      if (groups_[i].subgroups_.empty())
      {
        known_groups.insert(groups_[i].name_);
        update = true;
      }
      else
      {
        bool ok = true;
        for (std::size_t j = 0 ; ok && j < groups_[i].subgroups_.size() ; ++j)
          if (known_groups.find(groups_[i].subgroups_[j]) == known_groups.end())
            ok = false;
        if (ok)
        {
          known_groups.insert(groups_[i].name_);
          update = true;
        }
      }
    }
  }
  
  // if there are erroneous groups, keep only the valid ones
  if (known_groups.size() != groups_.size())
  {
    std::vector<Group> correct;
    for (std::size_t i = 0 ; i < groups_.size() ; ++i)
      if (known_groups.find(groups_[i].name_) != known_groups.end())
        correct.push_back(groups_[i]);
      else
        ROS_ERROR("Group '%s' has unsatisfied subgroups", groups_[i].name_.c_str());
    groups_.swap(correct);
  }
}

void srdf::Model::loadGroupStates(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* gstate_xml = robot_xml->FirstChildElement("group_state"); gstate_xml; gstate_xml = gstate_xml->NextSiblingElement("group_state"))
  {
    const char *sname = gstate_xml->Attribute("name");
    const char *gname = gstate_xml->Attribute("group");
    if (!sname)
    {
      ROS_ERROR("Name of group state is not specified");
      continue;
    }
    if (!gname)
    {
      ROS_ERROR("Name of group for state '%s' is not specified", sname);
      continue;
    }
    
    GroupState gs;
    gs.name_ = boost::trim_copy(std::string(sname));
    gs.group_ = boost::trim_copy(std::string(gname));
    
    bool found = false;
    for (std::size_t k = 0 ; k < groups_.size() ; ++k)
      if (groups_[k].name_ == gs.group_)
      {
        found = true;
        break;
      }
    if (!found)
    {
      ROS_ERROR("Group state '%s' specified for group '%s', but that group is not known", sname, gname);
      continue;
    }
    
    // get the joint values in the group state
    for (TiXmlElement* joint_xml = gstate_xml->FirstChildElement("joint"); joint_xml; joint_xml = joint_xml->NextSiblingElement("joint"))
    {
      const char *jname = joint_xml->Attribute("name");
      const char *jval = joint_xml->Attribute("value");
      if (!jname)
      {
        ROS_ERROR("Joint name not specified in group state '%s'", sname);
        continue;
      }
      if (!jval)
      {
        ROS_ERROR("Joint name not specified for joint '%s' in group state '%s'", jname, sname);
        continue;
      }
      std::string jname_str = boost::trim_copy(std::string(jname));
      if (!urdf_model.getJoint(jname_str))
      {
        bool missing = true;
        for (std::size_t k = 0 ; k < virtual_joints_.size() ; ++k)
          if (virtual_joints_[k].name_ == jname_str)
          {
            missing = false;
            break;
          }
        if (missing)
        {
          ROS_ERROR("Joint '%s' declared as part of group state '%s' is not known to the URDF", jname, sname);
          continue;
        }
      }
      try
      {
        std::string jval_str = std::string(jval);
        std::stringstream ss(jval_str);
        while (ss.good() && !ss.eof())
        {
          std::string val; ss >> val >> std::ws;
          gs.joint_values_[jname_str].push_back(boost::lexical_cast<double>(val));
        }
      }
      catch (boost::bad_lexical_cast &e)
      {
        ROS_ERROR("Unable to parse joint value '%s'", jval);
      }
      
      if (gs.joint_values_.empty())
        ROS_ERROR("Unable to parse joint value ('%s') for joint '%s' in group state '%s'", jval, jname, sname);
    }
    group_states_.push_back(gs);
  }
}

void srdf::Model::loadVisualSensors(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* s_xml = robot_xml->FirstChildElement("visual_sensor"); s_xml; s_xml = s_xml->NextSiblingElement("visual_sensor"))
  {
    const char *sname = s_xml->Attribute("name");
    const char *frame = s_xml->Attribute("frame");
    const char *fov_angle = s_xml->Attribute("fov_angle");
    const char *min_range = s_xml->Attribute("min_range");
    const char *max_range = s_xml->Attribute("max_range");
    if (!sname)
    {
      ROS_ERROR("Name of visual sensor is not specified");
      continue;
    }
    if (!frame)
    {
      ROS_ERROR("No frame specified for visual sensor '%s'", sname);
      continue;
    }
    if (!fov_angle)
    {
      ROS_ERROR("No field of view angle specified for visual sensor '%s'", sname);
      continue;
    }
    if (!min_range)
    {
      ROS_ERROR("No minimum range along Z axis specified for visual sensor '%s'", sname);
      continue;
    }
    if (!max_range)
    {
      ROS_ERROR("No maximum range along Z axis specified for visual sensor '%s'", sname);
      continue;
    }
    VisualSensor s;
    s.name_ = std::string(sname); boost::trim(s.name_);
    s.frame_ = std::string(frame); boost::trim(s.frame_);
    try
    {
      s.fov_angle_ = boost::lexical_cast<double>(std::string(fov_angle));
    }
    catch (boost::bad_lexical_cast &e)
    {     
      ROS_ERROR("Unable to parse field of view angle ('%s') for sensor '%s'", fov_angle, sname);
      continue;
    }
    try
    {
      s.min_range_ = boost::lexical_cast<double>(std::string(min_range));
    }
    catch (boost::bad_lexical_cast &e)
    {     
      ROS_ERROR("Unable to parse minimum range ('%s') for sensor '%s'", min_range, sname);
      continue;
    }
    try
    {
      s.max_range_ = boost::lexical_cast<double>(std::string(max_range));
    }
    catch (boost::bad_lexical_cast &e)
    {     
      ROS_ERROR("Unable to parse maximum range ('%s') for sensor '%s'", max_range, sname);
      continue;
    }
    visual_sensors_.push_back(s);
  }
}

void srdf::Model::loadEndEffectors(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* eef_xml = robot_xml->FirstChildElement("end_effector"); eef_xml; eef_xml = eef_xml->NextSiblingElement("end_effector"))
  {
    const char *ename = eef_xml->Attribute("name");
    const char *gname = eef_xml->Attribute("group");
    const char *parent = eef_xml->Attribute("parent_link");
    if (!ename)
    {
      ROS_ERROR("Name of end effector is not specified");
      continue;
    }
    if (!gname)
    {
      ROS_ERROR("Group not specified for end effector '%s'", ename);
      continue;
    }
    EndEffector e;
    e.name_ = std::string(ename); boost::trim(e.name_);
    e.component_group_ = std::string(gname); boost::trim(e.component_group_);
    bool found = false;
    for (std::size_t k = 0 ; k < groups_.size() ; ++k)
      if (groups_[k].name_ == e.component_group_)
      {
        found = true;
        break;
      }
    if (!found)
    {
      ROS_ERROR("End effector '%s' specified for group '%s', but that group is not known", ename, gname);
      continue;
    }
    if (!parent)
    {
      ROS_ERROR("Parent link not specified for end effector '%s'", ename);
      continue;
    }
    e.parent_link_ = std::string(parent); boost::trim(e.parent_link_);
    if (!urdf_model.getLink(e.parent_link_))
    {
      ROS_ERROR("Link '%s' specified as parent for end effector '%s' is not known to the URDF", parent, ename);
      continue;
    }
    end_effectors_.push_back(e);
  }
}

void srdf::Model::loadDisabledCollisions(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  for (TiXmlElement* c_xml = robot_xml->FirstChildElement("disable_collisions"); c_xml; c_xml = c_xml->NextSiblingElement("disable_collisions"))
  {
    const char *link1 = c_xml->Attribute("link1");
    const char *link2 = c_xml->Attribute("link2");
    if (!link1 || !link2)
    {
      ROS_ERROR("A pair of links needs to be specified to disable collisions");
      continue;
    }
    std::string link1_str = boost::trim_copy(std::string(link1));
    std::string link2_str = boost::trim_copy(std::string(link2));
    if (!urdf_model.getLink(link1_str))
    {
      ROS_ERROR("Link '%s' is not known to URDF. Cannot disable collisons.", link1);
      continue;
    }
    if (!urdf_model.getLink(link2_str))
    {
      ROS_ERROR("Link '%s' is not known to URDF. Cannot disable collisons.", link2);
      continue;
    }
    disabled_collisions_.push_back(std::make_pair(link1_str, link2_str));
  }
}

bool srdf::Model::initXml(const urdf::ModelInterface &urdf_model, TiXmlElement *robot_xml)
{
  clear();
  if (!robot_xml || robot_xml->ValueStr() != "robot")
  {
    ROS_ERROR("Could not find the 'robot' element in the xml file");
    return false;
  }
  
  // get the robot name
  const char *name = robot_xml->Attribute("name");
  if (!name)
    ROS_ERROR("No name given for the robot.");
  else
  {
    name_ = std::string(name); boost::trim(name_);
    if (name_ != urdf_model.getName())
      ROS_ERROR("Semantic description is not specified for the same robot as the URDF");
  }
  
  loadVirtualJoints(urdf_model, robot_xml);
  loadGroups(urdf_model, robot_xml);
  loadGroupStates(urdf_model, robot_xml);
  loadEndEffectors(urdf_model, robot_xml); 
  loadVisualSensors(urdf_model, robot_xml);
  loadDisabledCollisions(urdf_model, robot_xml);
  
  return true;
}

bool srdf::Model::initXml(const urdf::ModelInterface &urdf_model, TiXmlDocument *xml)
{
  TiXmlElement *robot_xml = xml ? xml->FirstChildElement("robot") : NULL;
  if (!robot_xml)
  {
    ROS_ERROR("Could not find the 'robot' element in the xml file");
    return false;
  }
  return initXml(urdf_model, robot_xml);
}


bool srdf::Model::initFile(const urdf::ModelInterface &urdf_model, const std::string& filename)
{
  // get the entire file
  std::string xml_string;
  std::fstream xml_file(filename.c_str(), std::fstream::in);
  if (xml_file.is_open())
  {
    while (xml_file.good())
    {
      std::string line;
      std::getline(xml_file, line);
      xml_string += (line + "\n");
    }
    xml_file.close();
    return initString(urdf_model, xml_string);
  }
  else
  {
    ROS_ERROR("Could not open file [%s] for parsing.", filename.c_str());
    return false;
  }
}

bool srdf::Model::initString(const urdf::ModelInterface &urdf_model, const std::string& xmlstring)
{
  TiXmlDocument xml_doc;
  xml_doc.Parse(xmlstring.c_str());
  return initXml(urdf_model, &xml_doc);
}


void srdf::Model::clear(void)
{
  name_ = "";
  groups_.clear();
  group_states_.clear();
  virtual_joints_.clear();
  end_effectors_.clear();
  visual_sensors_.clear();
  disabled_collisions_.clear();
}
