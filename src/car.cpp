#include "car.h"
#include "ports.h"

Car::Car()
{
    // Create components with unique ports
    this->components.push_back(new PowertrainComponent<NIC>(POWERTRAIN_PORT));
    this->components.push_back(new BrakeComponent<NIC>(BRAKE_PORT));
    //this->components.push_back(new SteeringComponent<NIC>(STEERING_PORT));
    //this->components.push_back(new TransmissionComponent<NIC>(TRANSMISSION_PORT));
    //this->components.push_back(new AirbagComponent<NIC>(AIRBAG_PORT));

    // Add more components here as needed
}

Car::~Car()
{
    // TODO: optionally delete components
}

void Car::start(int vm_id, int group_id, int total_sync_vms)
{
    int components_len = this->components.size();

    // Determine VM identity
    bool is_sync_master = ((vm_id % 10) == 0); // VMs 0, 10, 20, 30 are road-side equipment (gateway only)
    if (is_sync_master) components_len = 0;

    // Create the gateway for all VMs
    Gateway<NIC>* gateway = new Gateway<NIC>(GATEWAY_PORT, vm_id, total_sync_vms, is_sync_master);
    gateway->initialize(true, components_len, group_id);

    // Parent: build a static list of all component ports
    std::vector<uint16_t> all_ports;
    for (auto* comp : this->components) all_ports.push_back(comp->port());

    // Assign peer ports to each component (for fanout)
    for (auto* comp : this->components) comp->set_peer_ports(all_ports);

    // Only non-coordinator VMs (cars) fork child processes for other components
    if(!is_sync_master)
    {
        for (int i = 0; i < components_len; i++)
        {
            int pid = fork();
            if(pid == 0)
            {
                // Child process: initialize component
                CarComponent<NIC>* comp = this->components.at(i);
                uint8_t group_id = vm_id / 10;
                comp->initialize(false, components_len, group_id);
                return; // exit child after init
            }
        }
    }

    // Give the gateway the same peer list (so it can fanout)
    gateway->set_peer_ports(all_ports);

    // Run the gateway loop in the parent
    gateway->on_tick_loop();
}