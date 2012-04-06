//Flat Resolution (Parallel Implementation)
//Richard Barnes (rbarnes@umn.edu), 2012
//Develops an elevation mask which is guaranteed to drain
//a flat using a convergent flow pattern (unless it's a mesa)

#ifndef _flat_resolution_included
#define _flat_resolution_included

#include "utility.h"
#include "interface.h"
#include "data_structures.h"
#include <omp.h>
#include <deque>
#include <vector>
#include <queue>
#include "debug.h"
#include "tbb/concurrent_vector.h"
#include "tbb/concurrent_queue.h"

//Procedure:	BuildGradient
//Description:
//		The queues of edge cells developed in "find_flat_edges()" are copied
//		into the procedure. A breadth-first expansion labels cells by their
//		distance away from terrain of differing elevation. The maximal distance
//		encountered is noted.
//Inputs:
//		elevations		A 2D array of cell elevations
//		flowdirs		A 2D array indicating the flow direction of each cell
//		incrementations	A 2D array for storing incrementations
//		edges			A traversible FIFO queue developed in "find_flat_edges()"
//		flat_height		A vector with length equal to the maximum number of labels
//		labels			A 2D array which stores labels developed in "label_this()"
//Requirements:
//		incrementations	Is initiliazed to "0"
//Effects:
//		"incrementations" will contain the D8 distance of every flat cell from
//		terrain of differing elevation
//		"flat_height" will contain, for each flat, the maximal distance any
//		of its cells are from terrain of differing elevation
//Returns:
//		None
template <class T, class U>
void BuildGradient(const array2d<T> &elevations, const array2d<U> &flowdirs,
			int_2d &incrementations, std::deque<grid_cell> edges, 
			std::vector<int> &flat_height, const int_2d &labels){
	int x,y,nx,ny;
	int loops=1;
	grid_cell iteration_marker(-1,-1);

	diagnostic("Performing a Barnes flat resolution step...");

	//Incrementation
	edges.push_back(iteration_marker);
	while(edges.size()!=1){	//Only iteration marker is left in the end
		x=edges.front().x;
		y=edges.front().y;
		edges.pop_front();

		if(x==-1){	//I'm an iteration marker
			loops++;
			edges.push_back(iteration_marker);
			continue;
		}

		if(incrementations(x,y)>0) continue;	//I've already been incremented!

		//If I incremented, maybe my neighbours should too
		incrementations(x,y)=loops;
		flat_height[labels(x,y)]=loops;
		for(int n=1;n<=8;n++){
			nx=x+dx[n];	
			ny=y+dy[n];
			if(IN_GRID(nx,ny,elevations.width(),elevations.height()) 
					&& elevations(nx,ny)==elevations(x,y) 
					&& flowdirs(nx,ny)==NO_FLOW)
				edges.push_back(grid_cell(nx,ny));
		}
	}

	diagnostic("succeeded!\n");
}

//Procedure:	BarnesStep3
//Description:
//		The incrementation arrays developed in "BuildGradient()" are combined.
//		The maximal D8 distances of the gradient away from higher terrain,
//		which were stored in "flat_height" in "BuildGradient()" are used to
//		invert the gradient away from higher terrain. The result is an
//		elevation mask which has convergent flow characteristics and is
//		guaranteed to drain the flat.
//Inputs:
//		elevations	A 2D array of cell elevations
//		towards		A 2D array of incrementations towards lower terrain
//					Developed in "BuildGradient()" from "low_edges"
//		away		A 2D array of incrementations away from higher terrain
//					Developed in "BuildGradient()" from "high_edges"
//		flat_resolution_mask
//					A 2D array which will hold the combined gradients
//		edge		A FIFO queue which is used as a seed ("low_edges" should be passed)
//		flat_height	A vector with length equal to the maximum number of labels
//					It contains, for each flat, the maximal D8 distance
//					of any cell in that flat from higher terrain
//					Developed in "BuildGradient()"
//		labels		A 2D array which stores labels developed in "label_this()"
//Requirements:
//		flat_resolution_mask
//					Is initiliazed to "-1", which is the mask value
//Effects:
//		"flat_resolution_mask" will contain a weighted combination of
//		"towards" and "away" the D8 distance of every flat cell from
//		terrain of differing elevation.
//		"towards" has every flat cell set to "-1"
//		"edge" is emptied, except for an iteration marker
//Returns:
//		None
template <class T>
void CombineGradients(const array2d<T> &elevations, int_2d &towards, int_2d &away,
			int_2d &flat_resolution_mask, std::deque<grid_cell> &edge,
			const std::vector<int> &flat_height, const int_2d &labels){
	int x,y,nx,ny;

	diagnostic("Combining Barnes flat resolution steps...");

	while(edge.size()!=0){
		x=edge.front().x;
		y=edge.front().y;
		edge.pop_front();

		if(towards(x,y)==-1) continue;

		for(int n=1;n<=8;n++){
			nx=x+dx[n];
			ny=y+dy[n];
			if(IN_GRID(nx,ny,elevations.width(),elevations.height()) && elevations(nx,ny)==elevations(x,y))
				edge.push_back(grid_cell(nx,ny));
		}
		if(towards(x,y)>0){
			flat_resolution_mask(x,y)=2*(towards(x,y)-1);
			if(away(x,y)>0)
				flat_resolution_mask(x,y)+=flat_height[labels(x,y)]-away(x,y)+1;
		}
			
		towards(x,y)=-1;
	}

	diagnostic("succeeded!\n");
}


//Procedure:	label_this
//Description:
//		Performs a flood fill operation which labels all the cells of a flat
//		with a common label. Each flat will have a unique label
//Inputs:
//		x,y			Coordinates of seed for flood fill (a low edge cell)
//		label		The label to apply to the cells
//		labels		A 2D array containing the labels.
//		elevations	A 2D array of cell elevations
//Requirements:
//		labels		Is initialized to "-1"
//Effects:
//		The 2D array "labels" is modified such that each cell
//		which can be reached from (x,y) while traversing only
//		cells which share the same elevation as (x,y)
//		is labeled with "label"
//Returns:
//		None
template<class T>
void label_this(int x, int y, const int label, int_2d &labels, const array2d<T> &elevations){
	std::queue<grid_cell> to_fill;
	to_fill.push(grid_cell(x,y));
	const T target_elevation=elevations(x,y);

	while(to_fill.size()>0){
		x=to_fill.front().x;
		y=to_fill.front().y;
		to_fill.pop();
		if(elevations(x,y)!=target_elevation || labels(x,y)>-1) continue;
		labels(x,y)=label;
		for(int n=1;n<=8;n++)
			if(IN_GRID(x+dx[n],y+dy[n],labels.width(),labels.height()))
				to_fill.push(grid_cell(x+dx[n],y+dy[n]));
	}
}

//Procedure:	find_flat_edges
//Description:
//		Cells adjacent to lower and higher terrain are identified and
//		added to the appropriate queue
//Inputs:
//		low_edges	A traversable FIFO queue for storing cells adjacent to lower terrain
//		high_edges	A traversable FIFO queue for storing cells adjacent to higher terrain
//		flowdirs	A 2D array indicating the flow direction of each cell
//		elevations	A 2D array of cell elevations
//Requirements:
//		flowdirs	Cells without a defined flow direction have the value NO_FLOW
//Effects:
//		"low_edges" & "high_edges" are populated with cells adjacent to
//		lower and higher terrain, respectively
//Returns:
//		None
template <class T, class U>
void find_flat_edges(std::deque<grid_cell> &low_edges, std::deque<grid_cell> &high_edges,
			const array2d<U> &flowdirs, const array2d<T> &elevations){
	int nx,ny;
	diagnostic("\r\033[2KSearching for flats...\n");
	progress_bar(-1);
	for(int x=0;x<flowdirs.width();x++){
		progress_bar(x*omp_get_num_threads()*flowdirs.height()*100/(flowdirs.width()*flowdirs.height()));
		for(int y=0;y<flowdirs.height();y++){
			if(flowdirs(x,y)==flowdirs.no_data)
				continue;
			for(int n=1;n<=8;n++){
				nx=x+dx[n];
				ny=y+dy[n];

				if(!IN_GRID(nx,ny,flowdirs.width(),flowdirs.height())) continue;
				if(flowdirs(nx,ny)==flowdirs.no_data) continue;

				if(flowdirs(x,y)!=NO_FLOW && flowdirs(nx,ny)==NO_FLOW && elevations(nx,ny)==elevations(x,y)){
					low_edges.push_back(grid_cell(x,y));
					break;
				} else if(flowdirs(x,y)==NO_FLOW && elevations(x,y)<elevations(nx,ny)){
					high_edges.push_back(grid_cell(x,y));
					break;
				}
			}
		}
	}
	diagnostic_arg("\t\033[96msucceeded in %.2lfs.\033[39m\n",progress_bar(-1));
}

template <class T, class U>
void resolve_flats_barnes(const array2d<T> &elevations, const array2d<U> &flowdirs,
			int_2d &flat_resolution_mask, int_2d &labels){
	std::deque<grid_cell> low_edges,high_edges;	//TODO: Need estimate of size

	diagnostic_arg("The labels matrix will require approximately %ldMB of RAM.\n",
				flowdirs.width()*flowdirs.height()*sizeof(int)/1024/1024);
	diagnostic("Setting up labels matrix...");
	labels.resize(flowdirs.width(),flowdirs.height(),false);
	labels.init(-1);
	diagnostic("succeeded.\n");

	diagnostic_arg("The flat resolution mask will require approximately %ldMB of RAM.\n",
				flowdirs.width()*flowdirs.height()*sizeof(int)/1024/1024);
	diagnostic("Setting up flat resolution mask...");
	flat_resolution_mask.resize(flowdirs.width(),flowdirs.height(),false);
	flat_resolution_mask.init(-1);
	flat_resolution_mask.no_data=-1;
	diagnostic("succeeded!\n");

	find_flat_edges(low_edges, high_edges, flowdirs, elevations);

	if(low_edges.size()==0){
		if(high_edges.size()>0)
			diagnostic("There were flats, but none of them had outlets!\n");
		else
			diagnostic("There were no flats!\n");
		return;
	}

	diagnostic("Labeling flats...");
	int group_number=0;
	for(std::deque<grid_cell>::iterator i=low_edges.begin();i!=low_edges.end();i++)
		if(labels(i->x,i->y)==-1)
			label_this(i->x, i->y, group_number++, labels, elevations);
	diagnostic("succeeded!\n");

	diagnostic_arg("Found %d unique flats.\n",group_number);

	diagnostic("Removing flats without outlets from the queue...");
	std::deque<grid_cell> temp;
	for(std::deque<grid_cell>::iterator i=high_edges.begin();i!=high_edges.end();i++)
		if(labels(i->x,i->y)!=-1)
			temp.push_back(*i);
	diagnostic("succeeded.\n");

	if(temp.size()<high_edges.size())	//TODO: Prompt for intervention?
		diagnostic("\033[91mNot all flats have outlets; the DEM contains sinks/pits/depressions!\033[39m\n");
	high_edges=temp;
	temp.clear();

	diagnostic_arg("The incrementation matricies will require approximately %ldMB of RAM.\n",
				2*flowdirs.width()*flowdirs.height()*sizeof(int)/1024/1024);
	diagnostic("Setting up incrementation matricies...");
	int_2d towards(elevations,true);
	int_2d away(elevations,true);
	towards.init(0);
	away.init(0);
	diagnostic("succeeded!\n");

	diagnostic_arg("The flat height vector will require approximately %ldMB of RAM.\n",
				group_number*sizeof(int)/1024/1024);
	diagnostic("Creating flat height vector...");
	std::vector<int> flat_height(group_number);
	diagnostic("succeeded!\n");

	BuildGradient(elevations, flowdirs, towards, low_edges, flat_height, labels);
	BuildGradient(elevations, flowdirs, away, high_edges, flat_height, labels); //Flat_height overwritten here

	CombineGradients(elevations, towards, away, flat_resolution_mask, low_edges, flat_height, labels);
}

#endif