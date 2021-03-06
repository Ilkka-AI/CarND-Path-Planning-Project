#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }


int indexofSmallestElement(vector<double>& array, int size)
{
  int index = 0 ;
  double n = array[0] ;
  for (int i = 1; i < size; ++i)
  {
    if (array[i] < n)
    {
        n = array[i] ;
        index = i ;
    }
  }
 return index;
}


// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }
  
    // Set staring velocity, starting lane and starting state
	double  ref_vel=1;
    int lane=1;
	int current_state=0;
  h.onMessage([&lane,&ref_vel,&current_state,&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

			int prev_size=previous_path_x.size();


			if(prev_size >0)
			{
				car_s=end_path_s;
				
			}

			



// Process sensor fusion data for all lanes for state machine

  			
 			bool too_close_lane0 = false;
			bool too_close_lane1 = false;
			bool too_close_lane2 = false;
		
		
			
			vector<bool> too_close_all_lanes(3);
			vector<bool> too_close_all_lanes_back(3);
			vector<bool> too_close_all_lanes_front(3);
			// Position and velocity of the closest car in front of us on all lanes
			vector<double> closest_car_s(3);
			vector<double> closest_car_v(3);
			// Position and velocity of the closest car on our back for all lanes
			vector<double> closest_car_back_s(3);
			vector<double> closest_car_back_v(3);
			
			// initialize vectors
			for(int lanes=0;lanes<3;lanes++){
			too_close_all_lanes[lanes]=false;
			too_close_all_lanes_front[lanes]=false;
			too_close_all_lanes_back[lanes]=false;
			closest_car_v[lanes]=-1;
		
			}
		
 			
			// Loop through all vehicles detected by sensor fusion and process their posion and velocity data
			for(int i=0; i<sensor_fusion.size(); i++){
				// Get Frenet d coordinate, 
				float d=sensor_fusion[i][6];
				
				// Check through all 3 lanes
				for(int lanes=0;lanes<3;lanes++){
				// Assign car to a lane according to its position
				if(d<(2+4*lanes+2) && d>(2+4*lanes-2)) 
				{
					double vx = sensor_fusion[i][3];
					double vy = sensor_fusion[i][4];
					// Car speed
					double check_speed=sqrt(vx*vx+vy*vy);
					// Position s  in Frenet
					double check_car_s =sensor_fusion[i][5];
					// Predict future position
					check_car_s+=((double)prev_size*.02*check_speed);
					
					// Check cars in front of us on this lane
					if( (check_car_s-car_s)<30 && (check_car_s-car_s)>0 ) 
					{
					if(too_close_all_lanes_front[lanes]==false){
					// If this is the first car on same lane, set it as the closest car and assign its speed
					too_close_all_lanes_front[lanes]=true;
					// Calculate distance to the car
					closest_car_s[lanes]=(check_car_s-car_s);
					closest_car_v[lanes]=check_speed*2.23; // Transform to miles per hour
					} else{
					too_close_all_lanes_front[lanes]=true;
					// If there's another car closer on the same lane, update it as the closest car and set its speed
					if((check_car_s-car_s)<closest_car_s[lanes]){
					closest_car_s[lanes]=(check_car_s-car_s);
					closest_car_v[lanes]=check_speed*2.23; // Transform to miles per hour
					}
					
					}
					// Mark that there is a car too close on this lane
					too_close_all_lanes[lanes]=true;
											
					}
					
					// Check if the car is behind us
					if((check_car_s-car_s)>(-20) && (check_car_s-car_s)<0)
					{
					if(too_close_all_lanes_back[lanes]==false){
					// If this is the first car on same lane, set it as the closest car behind us and give its speed
					too_close_all_lanes_back[lanes]=true;
					// Calculate distance to the car
					closest_car_back_s[lanes]=(check_car_s-car_s);
					closest_car_back_v[lanes]=check_speed*2.23;
					} else{
					// If there's another car closer behind us on the same lane, update it as the closest car behind us and set its speed
					too_close_all_lanes_back[lanes]=true;
					if((check_car_s-car_s)>closest_car_s[lanes]){
					closest_car_back_s[lanes]=(check_car_s-car_s);
					closest_car_back_v[lanes]=check_speed*2.23;// Transform to miles per hour
					}
					
					}
					// Set that there is a car too close on this lane
					too_close_all_lanes_back[lanes]=true;
					}										
				}
			}
}
		
		
			
			// ###############################################################
			// Finite state machine
			
			// We use a finite state machine with 5 states for behavior planning, each having a cost function to decide possible transions to other states
			// Some states allow a self-transition, staying in the same state
			// At each time step we choose the transition that has the lowest cost (can be a self-transition)			
			
			// The following 5 states are considered
			// 0 keep current lane // 1 prepare to change left // 2 prepare to change right // 3 change to left // 4 change to right
			
			// Allowed transitions are
			// 0 - 0,1,2  
			// 1 - 0,1,2,3
			// 2 - 0,1,2,4
			// 3 - 0
			// 4 - 0
			
			// Initialize costs vector. Unaccepted transitions are give weight 10 and not touched later.
			// Allowed transitions will be later given a weight between 0 and 1
			vector<double> costs(5);
			for(int costs_i =0;costs_i<5;costs_i++){
				costs[costs_i]=10;
			}
			
			// Check current state
			switch(current_state)	{
			
			case 0: // State 0: keep current lane
			  
			  if(too_close_all_lanes_front[lane]){
			  // If there is a car in front of us too close, increase the cost of staying on the lane higher than preparing to change lanes
			    costs[0]=0.5; 
			  }
			  else{
			  // If there is no car too close in the front, keep on the lane
			    costs[0]=0;
			  }
			 
			 // If our car is already on the right, we cannot change right. Otherwise, we can start preparing to change lanes
			  if(lane==2){
			    costs[2]=1;
			  }else{
			    costs[2]=0.4;
			  }
			  // If our car is already on the left, we cannot change left. Otherwise, we can start preparing to change lanes. 
			  // Chancing to the left is more preferable than changing to the right if both are possible
			  if(lane==0){
			    costs[1]=1;
			  }else{
			    costs[1]=0.3;
			  }
			  
			  // In state 0, effectively the cost of maintaining a speed lower than allowed has a higher cost than increasing the speed towards the limit
			  // We increase speed if it is lower than allowed
			  // Effectively, a speed higher than the limit 49.5mph has an infinite cost and is not possible  
			  if(ref_vel<49.5){
			    ref_vel+=.424;}
 			  
			  // Choose state transion that has the lowest cost
			  current_state=indexofSmallestElement(costs,5);			    			  
			  break;
			  
			case 1: // State 1: prepare to change left
			  // If there isn't a car too close in the front or behind on the left lane, transition to state 3 is preferred
			  // If too close, transition is denied
			  if(too_close_all_lanes_front[lane-1]==false && too_close_all_lanes_back[lane-1]==false){
			    costs[3]=0;
			  }else{
			    costs[3]=1;
			  }
			  
			  // If already on the right, cannot transition to state 2
			  if(lane==2){
			    costs[2]=1;
			  }else{ // Otherwise, we can consider preparing to change right, if it doesnt have another car too close
			    if(too_close_all_lanes_front[lane+1]==false && too_close_all_lanes_back[lane+1]==false){
			      costs[2]=0.5;
			    }else{
			      costs[2]=1;
			    }
			
			  }
			  // If the car in front of gets further away fron us, we can consider transition back to state 0
			  if(too_close_all_lanes_front[lane]==false ){
			  	costs[0]=0.8;
				
			  }
			  // If all cars in front of us on our lane disappear, transition to state 0
			   if(closest_car_v[lane]==-1){
			  	costs[0]=0;
				
			  }
			  // If we are approaching the car, reduce speed until matching velocity
  			  if(ref_vel>closest_car_v[lane]){
			    ref_vel-=.324;
 			 cout << "car too close, breaking. Closest car vel is " << closest_car_v[lane] << "\n" ;
				}
				// Accelerate, if needed, to match speed
			  else{ref_vel+=.424;
			  cout << "accelerating again \n";
			  }
			  // Stay on this state if other transitions are not preferable
			  costs[1]=0.7;
			
			  // Choose the state transition that has the lowest cost
			  current_state=indexofSmallestElement(costs,5);
			  break;
			  
			case 2: // State 2: Preparing to change right
			  // See comments of state 1 (prepare to change left). This is just its mirror image. 
			  if(too_close_all_lanes_front[lane+1]==false && too_close_all_lanes_back[lane+1]==false){
			    costs[4]=0;
			  }else{
			    costs[4]=1;
			  }
			  
			  if(lane==0){
			    costs[1]=1;
			  }else{
			    if(too_close_all_lanes_front[lane-1]==false && too_close_all_lanes_back[lane-1]==false){
			      costs[1]=0.5;
			    }else{
			      costs[1]=1;
			    }

			  }
  			  if(ref_vel>closest_car_v[lane]){
			    cout << "car too close, breaking. Closest car vel is " << closest_car_v[lane] << "\n" ;
				ref_vel-=.324;}
			  else{
			    cout << "accelerating again \n";
			    ref_vel+=.424;}
			  costs[2]=0.7;
			
			  if(too_close_all_lanes_front[lane]==false ){
			  	costs[0]=0.8;
				
			  }
  			
			  if(closest_car_v[lane]==-1){
			  	costs[0]=0;
				
			  } 
			
			  current_state=indexofSmallestElement(costs,5);
			  break;
			
			case 3: // State 3: change to left
			lane=lane-1;
			// The only possible action is to change to left and transition back to state 0 
			current_state=0;
			break;
			
			case 4: //State 4: change to right
			lane=lane+1;
			// The only possible action is to change to right and transition back to state 0 
			current_state=0;
			break;
			}
			
			cout << "current_state " << current_state << "\n";
 			


		   // As trajectory generation I just use the method presented in the project walkthrough
		   // It needs as inputs the chosen reference speed and lane to follow (or change to) 
			vector<double> ptsx;
			vector<double> ptsy;
					  			
			double ref_x = car_x;
			double ref_y = car_y;
			double ref_yaw =deg2rad(car_yaw);
			
			if(prev_size<2)
			{
				double prev_car_x = car_x -cos(car_yaw);
				double prev_car_y = car_y -sin(car_yaw);
				
				ptsx.push_back(prev_car_x);
				ptsx.push_back(car_x);
				
				ptsy.push_back(prev_car_y);
				ptsy.push_back(car_y);
				
				}
			else{
				
				ref_x=previous_path_x[prev_size-1];
				ref_y=previous_path_y[prev_size-1];
				
				double ref_x_prev = previous_path_x[prev_size-2];
				double ref_y_prev = previous_path_y[prev_size-2];
				ref_yaw = atan2(ref_y-ref_y_prev,ref_x-ref_x_prev);
				
				ptsx.push_back(ref_x_prev);
				ptsx.push_back(ref_x);
				
				ptsy.push_back(ref_y_prev);
				ptsy.push_back(ref_y);
				
			}

			vector<double> next_wp0=getXY(car_s+30,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
			vector<double> next_wp1=getXY(car_s+60,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
			vector<double> next_wp2=getXY(car_s+90,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);

			ptsx.push_back(next_wp0[0]);
			ptsx.push_back(next_wp1[0]);
			ptsx.push_back(next_wp2[0]);
			
			ptsy.push_back(next_wp0[1]);
			ptsy.push_back(next_wp1[1]);
			ptsy.push_back(next_wp2[1]);

          	vector<double> next_x_vals;
          	vector<double> next_y_vals;
			
			for (int i=0;i<ptsx.size();i++){
				double shift_x=ptsx[i]-ref_x;
				double shift_y=ptsy[i]-ref_y;
				
				ptsx[i]=(shift_x*cos(0-ref_yaw)-shift_y*sin(0-ref_yaw));
				ptsy[i]=(shift_x*sin(0-ref_yaw)+shift_y*cos(0-ref_yaw));
			    
			}
			
			
			tk::spline s;
			s.set_points(ptsx,ptsy);
			
			
			
			for(int i=0;i<previous_path_x.size();i++){
				
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
				
			}
			
			double target_x =30.0;
			double target_y=s(target_x);
			double target_dist =sqrt((target_x)*(target_x)+(target_y)*(target_y));
			double x_add_on =0;
			
			for(int i=1; i<=50-previous_path_x.size();i++){
				
				double N = (target_dist/(.02*ref_vel/2.24));
				double x_point=x_add_on+(target_x)/N;
				double y_point=s(x_point);
				
				x_add_on=x_point;
			
				double x_ref=x_point;
				double y_ref=y_point;
				
				x_point =(x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw));
				y_point =(x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw));
				
				x_point+=ref_x;
				y_point+=ref_y;
				
				next_x_vals.push_back(x_point);
				next_y_vals.push_back(y_point);
			}



          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

			
          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
