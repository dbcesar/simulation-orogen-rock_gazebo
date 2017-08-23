/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */
//======================================================================================
// Brazilian Institute of Robotics 
// Authors: Thomio Watanabe
// Date: December 2014
//====================================================================================== 

#include "ModelTask.hpp"
#include <gazebo/common/Exception.hh>

using namespace std;
using namespace gazebo;
using namespace rock_gazebo;

ModelTask::ModelTask(string const& name)
	: ModelTaskBase(name)
{
    _joint_command_timeout.set(base::Time::fromSeconds(1.0));
    _cov_position.set(base::Matrix3d::Ones() * base::unset<double>());
    _cov_orientation.set(base::Matrix3d::Ones() * base::unset<double>());
    _cov_velocity.set(base::Matrix3d::Ones() * base::unset<double>());
}
ModelTask::ModelTask(string const& name, RTT::ExecutionEngine* engine)
	: ModelTaskBase(name, engine)
{
}

ModelTask::~ModelTask()
{
    releaseLinks();
}

void ModelTask::setGazeboModel(WorldPtr _world,  ModelPtr _model)
{
    string name = "gazebo:" + _world->GetName() + ":" + _model->GetName();
    provides()->setName(name);
    _name.set(name);

    BaseTask::setGazeboWorld(_world);
    model = _model;

    if (_model_frame.get().empty())
        _model_frame.set(_model->GetName());
    if (_world_frame.get().empty())
        _world_frame.set(_world->GetName());
} 

void ModelTask::setupJoints()
{
    // Get all joints from a model and set Rock Input/Output Ports
    for(Joint_V::iterator joint = gazebo_joints.begin(); joint != gazebo_joints.end(); ++joint)
    {
#if GAZEBO_MAJOR_VERSION >= 6
        if((*joint)->HasType(physics::Base::FIXED_JOINT))
        {
            gzmsg << "ModelTask: ignore fixed joint: " << world->GetName() + "/" + model->GetName() +
                "/" + (*joint)->GetName() << endl;
            continue;
        }
#endif
        gzmsg << "ModelTask: found joint: " << world->GetName() + "/" + model->GetName() +
                "/" + (*joint)->GetName() << endl;
        joints_in.names.push_back( (*joint)->GetName() );
        joints_in.elements.push_back( base::JointState::Effort(0.0) );
    }
}

void ModelTask::setupLinks()
{

    // The robot configuration YAML file must define the exported links.
    vector<LinkExport> export_conf = _exported_links.get();

    for(vector<LinkExport>::iterator it = export_conf.begin();
            it != export_conf.end(); ++it)
    {
        ExportedLink exported_link(*it);

        exported_link.source_link =
            checkExportedLinkElements("source_link", it->source_link, _world_frame.get());
        exported_link.target_link =
            checkExportedLinkElements("target_link", it->target_link, _world_frame.get());
        exported_link.source_frame =
            checkExportedLinkElements("source_frame", it->source_frame, exported_link.source_link);
        exported_link.target_frame =
            checkExportedLinkElements("target_frame", it->target_frame, exported_link.target_link);

        if (it->source_link != _world_frame.get())
            exported_link.source_link_ptr = model->GetLink( it->source_link );
        if (it->target_link != _world_frame.get())
            exported_link.target_link_ptr = model->GetLink( it->target_link );
        exported_link.port_name = it->port_name;
        exported_link.port_period = it->port_period;

        if (exported_link.source_link != _world_frame.get() && !exported_link.source_link_ptr)
        { gzthrow("ModelTask: cannot find exported source link " << it->source_link << " in model"); }
        else if (exported_link.target_link != _world_frame.get() && !exported_link.target_link_ptr)
        { gzthrow("ModelTask: cannot find exported target link " << it->target_link << " in model"); }
        else if (it->port_name.empty())
        { gzthrow("ModelTask: no port name given in link export"); }
        else if (ports()->getPort(it->port_name))
        { gzthrow("ModelTask: provided port name " << it->port_name << " already used on the task interface"); }
        else if (exported_links.find(it->port_name) != exported_links.end())
        { gzthrow("ModelTask: provided port name " << it->port_name << " already used by another exported link"); }

        exported_links.insert(make_pair(it->port_name, exported_link));
    }

    for (ExportedLinks::iterator it = exported_links.begin(); it != exported_links.end(); ++it)
    {
        // Create the ports dynamicaly
        gzmsg << "ModelTask: exporting link "
            << world->GetName() + "/" + model->GetName() + "/" + it->second.source_link << "2" << it->second.target_link
            << " through port " << it->first << " updated every " << it->second.port_period.toSeconds() << " seconds."
            << endl;

        it->second.port = new RBSOutPort( it->first );
        ports()->addPort(*it->second.port);
    }
}

bool ModelTask::startHook()
{
    if (! ModelTaskBase::startHook())
        return false;

    lastCommandTime = base::Time();

    for(ExportedLinks::iterator it = exported_links.begin(); it != exported_links.end(); ++it)
    {
        it->second.last_update.fromSeconds(0);
    }

    return true;
}
void ModelTask::updateHook()
{
    base::Time time = getCurrentTime();

    base::samples::RigidBodyState modelPose;
    if (_model_pose.read(modelPose) == RTT::NewData)
        warpModel(modelPose);

    updateModelPose(time);
    updateJoints(time);
    updateLinks(time);
}

void ModelTask::warpModel(base::samples::RigidBodyState const& modelPose)
{
    Eigen::Vector3d v(modelPose.position);
    math::Vector3 model2world_v(v.x(), v.y(), v.z());
    Eigen::Quaterniond q(modelPose.orientation);
    math::Quaternion model2world_q(q.w(), q.x(), q.y(), q.z());
    math::Pose model2world;
    model2world.Set(model2world_v, model2world_q);
    model->SetWorldPose(model2world);
}

void ModelTask::updateModelPose(base::Time const& time)
{
    math::Pose model2world = model->GetWorldPose();
    math::Vector3 model2world_angular_vel = model->GetRelativeAngularVel();
    math::Vector3 model2world_vel = model->GetWorldLinearVel();

    RigidBodyState rbs;
    rbs.invalidate();
    rbs.time = time;
    rbs.sourceFrame = _model_frame.get();
    rbs.targetFrame = _world_frame.get();
    rbs.position = base::Vector3d(
        model2world.pos.x,model2world.pos.y,model2world.pos.z);
    rbs.cov_position = _cov_position.get();
    rbs.orientation = base::Quaterniond(
        model2world.rot.w,model2world.rot.x,model2world.rot.y,model2world.rot.z );
    rbs.cov_orientation = _cov_orientation.get();
    rbs.velocity = base::Vector3d(
        model2world_vel.x, model2world_vel.y, model2world_vel.z);
    rbs.cov_velocity = _cov_velocity.get();
    rbs.angular_velocity = base::Vector3d(
        model2world_angular_vel.x, model2world_angular_vel.y, model2world_angular_vel.z);
    _pose_samples.write(rbs);
}

void ModelTask::updateJoints(base::Time const& time)
{
    // Read joint pos and speed from gazebo link
    vector<string> names;
    vector<double> positions;
    vector<float> speeds;
    for(Joint_V::iterator it = gazebo_joints.begin(); it != gazebo_joints.end(); ++it )
    {
#if GAZEBO_MAJOR_VERSION >= 6
        // Do not export fixed joints
        if((*it)->HasType(physics::Base::FIXED_JOINT))
            continue;
#endif

        // Read joint angle from gazebo link
        names.push_back( (*it)->GetScopedName() );
        positions.push_back( (*it)->GetAngle(0).Radian() );
        speeds.push_back( (float)(*it)->GetVelocity(0) );
    }
    // fill the Joints samples and write
    base::samples::Joints joints = base::samples::Joints::Positions(positions,names);
    vector<float>::iterator speeds_it = speeds.begin();
    for(std::vector<base::JointState>::iterator elements_it = joints.elements.begin(); elements_it != joints.elements.end(); ++elements_it )
    {
        (*elements_it).speed = *speeds_it;
        ++speeds_it;
    }
    joints.time = time;
    _joints_samples.write( joints );

    RTT::FlowStatus flow = _joints_cmd.readNewest( joints_in );
    if (flow == RTT::NewData)
    {
        lastCommandTime = time;
        lastCommand = joints_in;
    }
    else if (lastCommandTime.isNull())
        return;
    else if (time - lastCommandTime >= _joint_command_timeout.get())
        return;
    else
        joints_in = lastCommand;

    const std::vector<std::string> &cmd_names= joints_in.names;
    for(Joint_V::iterator it = gazebo_joints.begin(); it != gazebo_joints.end(); ++it )
    {
#if GAZEBO_MAJOR_VERSION >= 6
        // Do not set fixed joints
        if((*it)->HasType(physics::Base::FIXED_JOINT))
            continue;
#endif

        // Do not set joints which are not part of the command
        auto result = find(cmd_names.begin(),cmd_names.end(),(*it)->GetScopedName());
        if(result == cmd_names.end())
            continue;

        // Apply effort to joint
        base::JointState j_cmd(joints_in[result-cmd_names.begin()]);
        if( j_cmd.isEffort() )
            (*it)->SetForce(0, j_cmd.effort );
        else if( j_cmd.isPosition() )
            (*it)->SetPosition(0, j_cmd.position );
        else if( j_cmd.isSpeed() )
            (*it)->SetVelocity(0, j_cmd.speed );
    }
}

void ModelTask::updateLinks(base::Time const& time)
{
    for(ExportedLinks::iterator it = exported_links.begin(); it != exported_links.end(); ++it)
    {
        //do not update the link if the last port writing happened
        //in less then link_period. 
        if (!(it->second.last_update.isNull()))
        {
            if ((time - it->second.last_update) <= it->second.port_period)
                return;
        }

        math::Pose source2world = math::Pose::Zero;
        math::Vector3 sourceInWorld_linear_vel  = math::Vector3::Zero;
        math::Vector3 source_angular_vel;
        if (it->second.source_link_ptr)
        {
            source2world = it->second.source_link_ptr->GetWorldPose();
            sourceInWorld_linear_vel  = it->second.source_link_ptr->GetWorldLinearVel();
            source_angular_vel        = it->second.source_link_ptr->GetRelativeAngularVel();
        }

        math::Pose target2world = math::Pose::Zero;
        if (it->second.target_link_ptr)
            target2world = it->second.target_link_ptr->GetWorldPose();

        math::Pose source2target( math::Pose(source2world - target2world) );
        math::Vector3 sourceInTarget_linear_vel (target2world.rot.RotateVectorReverse(sourceInWorld_linear_vel));

        RigidBodyState rbs;
        rbs.sourceFrame = it->second.source_frame;
        rbs.targetFrame = it->second.target_frame;
        rbs.position = base::Vector3d(
            source2target.pos.x,source2target.pos.y,source2target.pos.z);
        rbs.cov_position = it->second.cov_position;
        rbs.orientation = base::Quaterniond(
            source2target.rot.w,source2target.rot.x,source2target.rot.y,source2target.rot.z );
        rbs.cov_orientation = it->second.cov_orientation;
        rbs.velocity = base::Vector3d(
            sourceInTarget_linear_vel.x,sourceInTarget_linear_vel.y,sourceInTarget_linear_vel.z);
        rbs.cov_velocity = it->second.cov_velocity;
        rbs.angular_velocity = base::Vector3d(
            source_angular_vel.x,source_angular_vel.y,source_angular_vel.z);
        rbs.time = time;
        it->second.port->write(rbs);
        it->second.last_update = time;
    }
}

bool ModelTask::configureHook()
{
    if( ! ModelTaskBase::configureHook() )
        return false;

    // Test if setGazeboModel() has been called -> if world/model are NULL
    if( (!world) && (!model) )
        return false;

    gazebo_joints = model->GetJoints();
    setupLinks();
    setupJoints();

    return true;
}

void ModelTask::cleanupHook()
{
    ModelTaskBase::cleanupHook();
    releaseLinks();
}

void ModelTask::releaseLinks()
{
    for(ExportedLinks::iterator it = exported_links.begin(); it != exported_links.end(); ++it) {
        if (it->second.port != NULL) {
            ports()->removePort(it->second.port->getName());
            delete it->second.port;
            it->second.port = NULL;
        }
    }
    exported_links.clear();
}

string ModelTask::checkExportedLinkElements(string element_name, string test, string option)
{
    // when not defined, source_link and target_link will recieve "world".
    // when not defined, source_frame and target_frame will receive source_link and target_link content
    if( test.empty() )
    {
        gzmsg << "ModelTask: " << model->GetName() << " " << element_name << " not defined, using "<< option << endl;
        return option;
    }else {
        gzmsg << "ModelTask: " << model->GetName() << " " << element_name << ": " << test << endl;
        return test;
    }
}

