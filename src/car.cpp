#include "car.h"
#include "ports.h"

Car::Car()
{
    // Create components with unique ports
    this->components.push_back(new PowertrainComponent<NIC>(POWERTRAIN_PORT));
    this->components.push_back(new BrakeComponent<NIC>(BRAKE_PORT));
    // Add more components here as needed
}

Car::~Car()
{
    // TODO: optionally delete components
}

void Car::start()
{
    int components_len = this->components.size();

    // Create and initialize the gateway first
    Gateway<NIC>* gateway = new Gateway<NIC>(GATEWAY_PORT);
    gateway->initialize(true, components_len);

    // Fork child processes for each component
    for (int i = 0; i < components_len; i++)
    {
        int pid = fork();
        if(pid == 0)
        {
            CarComponent<NIC>* comp = this->components.at(i);
            comp->initialize(false, components_len);
            return; // child process exits start() after initialization
        }
    }

    // Parent: build a static list of all component ports
    std::vector<uint16_t> all_ports;
    for (auto* comp : this->components)
        all_ports.push_back(comp->port());

    // Assign peer ports to each component (for fanout)
    for (auto* comp : this->components)
        comp->set_peer_ports(all_ports);

    // Give the gateway the same peer list (so it can fanout)
    gateway->set_peer_ports(all_ports);

    // Run the gateway loop in the parent
    gateway->on_tick_loop();
}