/* Copyright (C) 2015-2017 Michele Colledanchise - All Rights Reserved
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
*   to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
*   and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
*   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <fallback_node_with_memory.h>
#include "actions/move_to_position.h"
#include <string>
#include <string>
#include "std_msgs/String.h"
#include <ros/ros.h>
#include <std_msgs/Bool.h> 

BT::FallbackNodeWithMemory::FallbackNodeWithMemory(std::string name) : ControlNode::ControlNode(name)
{
    reset_policy_ = BT::ON_SUCCESS_OR_FAILURE;
    current_child_idx_ = 0;  // initialize the current running child
    fallbackresult = false;
}


BT::FallbackNodeWithMemory::FallbackNodeWithMemory(std::string name, int reset_policy) : ControlNode::ControlNode(name)
{
    reset_policy_ = reset_policy;
    current_child_idx_ = 0;  // initialize the current running child
}


BT::FallbackNodeWithMemory::~FallbackNodeWithMemory() {}


BT::ReturnStatus BT::FallbackNodeWithMemory::Tick()
{   
    //std::this_thread::sleep_for(std::chrono::seconds(5));
    // Creazione di un array di due booleani
    bool results[2] = {true, true};
    


    //DEBUG_STDOUT(get_name() << " ticked, memory counter: "<< current_child_idx_);

    // Vector size initialization. N_of_children_ could change at runtime if you edit the tree
    N_of_children_ = children_nodes_.size();

    // Routing the ticks according to the fallback node's (with memory) logic:
    while (current_child_idx_ < N_of_children_)
    {
        /*      Ticking an action is different from ticking a condition. An action executed some portion of code in another thread.
                We want this thread detached so we can cancel its execution (when the action no longer receive ticks).
                Hence we cannot just call the method Tick() from the action as doing so will block the execution of the tree.
                For this reason if a child of this node is an action, then we send the tick using the tick engine. Otherwise we call the method Tick() and wait for the response.
        */
        if (children_nodes_[current_child_idx_]->get_type() == BT::ACTION_NODE)
        {
            // 1) If the child i is an action, read its state.
            // Action nodes runs in another thread, hence you cannot retrieve the status just by executing it.

            child_i_status_ = children_nodes_[current_child_idx_]->get_status();
            //DEBUG_STDOUT(get_name() << " It is an action " << children_nodes_[current_child_idx_]->get_name()
                        // << " with status: " << child_i_status_);

            if (child_i_status_ == BT::IDLE || child_i_status_ == BT::HALTED)
            {
                // 1.1) If the action status is not running, the sequence node sends a tick to it.
                //DEBUG_STDOUT(get_name() << " NEEDS TO TICK " << children_nodes_[current_child_idx_]->get_name());
                children_nodes_[current_child_idx_]->tick_engine.Tick();

                // waits for the tick to arrive to the child
                do
                {
                    child_i_status_ = children_nodes_[current_child_idx_]->get_status();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                while (child_i_status_ != BT::RUNNING && child_i_status_ != BT::SUCCESS
                      && child_i_status_ != BT::FAILURE);
            }
        }
        else
        {
            // 2) if it's not an action:
            // Send the tick and wait for the response;
            child_i_status_ = children_nodes_[current_child_idx_]->Tick();
        }


        if (child_i_status_ == BT::SUCCESS ||child_i_status_ == BT::FAILURE )
        {
            /*ros::NodeHandle n;
            ros::Publisher nodes_pub = n.advertise<std_msgs::String>("/topic_execution_bt", 1000);
            ros::Rate loop_rate(10); 
            int count = 0;
            bool should_continue = true;
            while (ros::ok() and should_continue) {

                // Controlla se dovresti uscire dal ciclo
                if (count == 1) {
                    should_continue = false;
                }
                       
                std_msgs::String nodes_msg;
                nodes_msg.data = "Routine in esecuzione!";
            
                DEBUG_STDOUT("Invio del messaggio: "<< nodes_msg.data);
                nodes_pub.publish(nodes_msg);
                ros::spinOnce();
                loop_rate.sleep();
                ++count;
            }*/
            // the child goes in idle if it has returned success or failure.
            // Ottenere il risultato dal primo figlio (nodo di azione) e salvarlo nell'array
            results[0] = children_nodes_[0]->get_result_value();
            DEBUG_STDOUT(" VALORE RESULTS[0] FALLBACK: " << results[0] << " !");
            // Ottenere il risultato dal secondo figlio (nodo di fault) e salvarlo nell'array
            results[1] = children_nodes_[1]->get_result_value();
            DEBUG_STDOUT(" VALORE RESULTS[1] FALLBACK: " << results[1] << " !");
            // Ora puoi utilizzare l'array 'results' per prendere decisioni nel tuo nodo di fallback.
            if (results[0] == false and results[1] == false) {
                set_result_value(false);
            }else{
                set_result_value(true);
            }

            children_nodes_[current_child_idx_]->set_status(BT::IDLE);
        }

        if (child_i_status_ != BT::FAILURE)
        {
            // If the  child status is not success, return the status
            //DEBUG_STDOUT("the status of: " << get_name() << " becomes " << child_i_status_);
            if (child_i_status_ == BT::SUCCESS && (reset_policy_ == BT::ON_SUCCESS
                                                  || reset_policy_ == BT::ON_SUCCESS_OR_FAILURE))
            {

                

                current_child_idx_ = 0;
            }

            set_status(child_i_status_);
            return child_i_status_;
        }
        else if (current_child_idx_ != N_of_children_ - 1)
        {
            // If the  child status is failure, continue to the next child
            // (if any, hence if(current_child_ != N_of_children_ - 1) ) in the for loop (if any).
            current_child_idx_++;
        }
        else
        {
            // If it the last child.
            if (child_i_status_ == BT::FAILURE)
            {
                // if it the last child and it has returned failure, reset the memory

                current_child_idx_ = 0;
            }

            set_status(child_i_status_);
            return child_i_status_;
        }
    }
    return BT::EXIT;
}


int BT::FallbackNodeWithMemory::DrawType()
{
    return BT::SELECTORSTAR;
}

void BT::FallbackNodeWithMemory::Halt()
{
    current_child_idx_ = 0;
    BT::ControlNode::Halt();
}