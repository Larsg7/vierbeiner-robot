#include <stdio.h>
#include <math.h>
#include <fstream>
#include <stdlib.h>
#include <string>

#include "walkcontroller.h"


using namespace std;
using namespace arma;

/**
 * here we initialize everything at the start of the run
 */
WalkController::WalkController()
  : AbstractController("walkcontroller", "$Id$"){
  t=0;
  totalTime = 0;
  addParameterDef("speed",&speed, 30.0);
  addParameterDef("sinMod",&sinMod, 10);
  addParameterDef("hipampl",&hipamplitude, 0.8);
  addParameterDef("kneeampl",&kneeamplitude, 0.8);
  addParameterDef("resetRobot",&resetRobot, 0);
  addParameterDef("numberOfGenerations",&numberOfGenerations, 100);

  number_sensors=0;
  number_motors=0;

  // initialize stuff
  startOfSim = true;
  endOfSim = false;
  highestFitness = 0;
  penalty = 0;
  useBestNetwork = false;
  takingVideo = false;

  // output files
  fitnessFile = "fitness";
  ofFile.open(fitnessFile);
  motorFile.open("motorOutput");

  srand(time(NULL));

  //// Network ////
  for (int i = 0; i < numberOfNetworks; ++i)
  {
    networkList.push_back(new Neural_Custom);
    networkList[i]->initNetwork(inputSize,outputSize,numberOfNeurons);
    networkList[i]->initWeightsRandom();
  }
  generationList.push_back(networkList);
  //// Network End ////

  // use a custom network
  useCustom = false;
  if (useCustom) {  // use higher powerfactor
    t = maxTime;
    generation = numberOfGenerations;

    mat inputW = { { -3.1385,21.8814},
                   { 0,  -0.3185} };

    mat outputW = { {8.9953,  -65.6696,   55.4174,   -0.7012,   -2.8553,   -2.9402,    1.2224,  0,   -1.5797,    2.2100},
                    {-0.6922,    5.5808,    0.0003,   15.7358,    1.1298,   -4.7488,    1.9581, 4.0304,    4.4995,    0.0092}};

    bestNetwork = new Neural_Custom;
    bestNetwork->initNetwork(inputSize,outputSize,numberOfNeurons);
    bestNetwork->setWeights(inputW,outputW);
    startOfSim = false;
    endOfSim = true;
  }
};

/**
 * set simulation related variables
 * @param sensornumber total number of sensors
 * @param motornumber  total number of motors
 */
void WalkController::init(int sensornumber, int motornumber, RandGen* randGen){
  number_sensors=sensornumber;
  number_motors=motornumber;
  if(motornumber < 12) {
    cerr << "Walkcontroller needs 12 motors!" << endl;
    exit(1);
  }
};

/**
 * runs at each step and does all the coordination about what should happen next
 * @param sensors      sensor-object containing all sensor-values
 * @param motors       motor-object containing all motor-values
 */
void WalkController::step(const sensor* sensors, int sensornumber,
                          motor* motors, int motornumber) {
  /** sensors/motors: 0: neck, 1: tail
                     2,3,4,5 : hip:  rh, lh, rf, lf
                     6,7,8,9 : knee: rh, lh, rf, lf
                     10,11   : ankle rh, lh
   */

  assert(sensornumber == 12+3);

  if (startOfSim) {
    cout << "Starting simulation." << endl;
    cout << "Are now using network " << curNetID << " from generation " << generation << endl;
    startOfSim = false;
  }

  // get starting position
  if (t==2) // wait two steps to get a good value
    for (int i = 0; i < 3; ++i) {
      startPos[i] = sensors[12+i];
    }
  else // get current position
    for (int i = 0; i < 3; ++i) {
      posArray[i] = sensors[12+i];
    }

  // current network in use
  curNet = !useCustom ? networkList[curNetID] : new Neural_Custom;

  // dirty way to run bestnetwork at the end of each generation for video taking purposes
  if (useBestNetwork && takingVideo) {
    if (t == 0)
      cout << "Using now bestNetwork! " << bestNetwork->getFitness() << endl;

    resetRobot = 0;
    forwardSensor(sensors, sensornumber, motors, motornumber, bestNetwork);
    t++;
    if (t > maxTime)
    {
      useBestNetwork = false;
      t = 0;
      cout << endl << "Are now using network " << curNetID << " from generation " << generation << endl;
      resetRobot = 1;
    }
  }

  else if (t < maxTime && !endOfSim) {
    // let simulation run
    forwardSensor(sensors, sensornumber, motors, motornumber, curNet);

    // calculate current fitness of the network
    curNet->setFitness(t > 2 ? max(curNet->getFitness(), calFitness(posArray)) : 0);

    resetRobot = 0;

    // step time forward
    endOfStep();
  }

  // at end of evaluation time
  else if (!endOfSim) {
    cout << "Network " << curNetID << " got a fitness of " << curNet->getFitness() << endl;

    if (curNetID < numberOfNetworks - 1) {
      startOfNewNet();  // move to next network
    }

    //at end of generation
    else if (generation < numberOfGenerations){
      cout << "Generation " << generation << " completed." << endl;
      useBestNetwork = true;

      startNextGen();   // breed new generation of networks
    }

    // at end of run
    else {
      startNextGen();

      cout << "Finished last generation!" << endl;
      cout << "Using now best network with fitness " << highestFitness << endl;
      endOfSim = true;

      cout << "Best inputWeights:" << endl;
      bestNetwork->inputWeights.print();
      cout << endl;
      cout << "Best outputWeights:" << endl;
      bestNetwork->outputWeights.print();
      cout << endl;
    }

    // just to get the message not when run ends
    if (!endOfSim && (!useBestNetwork || !takingVideo))
      cout << "Are now using network " << curNetID << " from generation " << generation << endl;

    resetRobot = 1;   // reset robot to starting position
  }

  // if endOfSim == true
  else {
    forwardSensor(sensors, sensornumber, motors, motornumber, bestNetwork);
    resetRobot = 0;
    endOfStep();
  }
};

/**
 * calculate the fitness of the current network
 * @param: posNow   array holding the current position of the robot
 * @return: the current fitness
 */
double WalkController::calFitness(double posNow[3]) {
  double distanceNow = 0.;
  double currentSpeed = 0.;

  int penStepSize = 5;

  // calculate current distance
  for (int i = 0; i < 1; ++i)
  {
    distanceNow += -pow(startPos[i] - posNow[i],1);
  }

  // calculate penalty
  if (t % penStepSize == 0 && t != 0) {
    currentSpeed = (distanceNow - distanceThen) / penStepSize;
    totalSpeed += currentSpeed;
    averageSpeed = totalSpeed / ((double)(t/penStepSize));
    //penalty += abs(averageSpeed - currentSpeed);
    distanceThen = distanceNow;
    //cout << averageSpeed << endl;
  }

  //return exp(pow(averageSpeed,2)) - 1;
  return exp((double)distanceNow) - 1;
}

/**
 * start a new generation and breed new networks
 */
void WalkController::startNextGen() {
  // clean nextNetworkList from networks of last generation
  nextNetworkList.erase(nextNetworkList.begin(), nextNetworkList.end());

  // first calculate sum of all fitnesses and get highest Fitness
  double totalFitness = 0;
  double thisHighestFitness = 0;
  double thisAverageFitness = 0;
  for (unsigned int i = 0; i < networkList.size(); ++i) {
    double thisFitness = networkList[i]->getFitness();

    if (highestFitness < thisFitness) {
      highestFitness = thisFitness;
      bestNetwork = networkList[i];
    }

    thisHighestFitness = max(thisFitness, thisHighestFitness);
    totalFitness += networkList[i]->getFitness();
  }

  thisAverageFitness = totalFitness/(double)numberOfNetworks;
  // print data into file
  ofFile << generation << "  " << thisHighestFitness << "  " << thisAverageFitness << endl;

  cout << "Generation " << generation << " hat an average fitness of " << thisAverageFitness << endl;
  cout << "And a highest fitness of " << thisHighestFitness << endl << endl;

  // Breed two networks to new one using fitness as probability
  for (int i = 0; i < numberOfNetworks; ++i) {

      double randn1 = (double)rand() / RAND_MAX;
      double randn2 = (double)rand() / RAND_MAX;

      double a = 0;
      int first = 0;      // first network to breed with
      int second = 0;     // second network to breed with

      for (int j = 0; j < numberOfNetworks; j++)
      {
          a += networkList[j]->getFitness() / totalFitness;

          first = (a > randn1 && first == 0) ? j : first;
          second = (a > randn2 && second == 0) ? j : second;

          // make sure not to use the same network to breed with
          if (first == second && first != 0 && second != 0)
              continue;
      }

      nextNetworkList.push_back(networkList[first]->breed(networkList[second]));
  }

  assert(nextNetworkList.size() == numberOfNetworks);

  networkList = nextNetworkList;

  generationList.push_back(nextNetworkList);

  //// Output to file ////
  // now run bestNetwork again but write output values in file
  if (bestNetwork != lastBestNetwork)
  {
    ofstream bestmotorOutput;
    bestmotorOutput.open("./motorData/motorOutput"+to_string(generation));

    for (int i = 0; i < maxTime; ++i) // using i as t
    {
      input(0,0) = sin(i/speed) * sinMod;
      input(0,1) = sin(i/speed + (M_PI/2)) * sinMod;

      output = bestNetwork->forward(input);

      bestmotorOutput << i << "\t";
      for (int j = 0; j < outputSize; ++j)
      {
        bestmotorOutput << 2 * output(0,j) - 1 << "\t";
      }
      bestmotorOutput << endl;
    }
    bestmotorOutput.close();
    lastBestNetwork = bestNetwork;
  }
  //// End output to file ////

  generation++;
  curNetID = 0;
}

/**
 * calculate motorvalues form input using the neural network given
 * @param neural       neural network to work with
 */
void WalkController::forwardSensor(const sensor* sensors, int sensornumber,
                          motor* motors, int motornumber, Neural_Custom* neural) {
  motors[0] = 0;
  motors[1] = 0;

  //stepNoLearning(sensors, sensornumber, motors, motornumber);

  input(0,0) = sin(t/speed) * sinMod;
  input(0,1) = sin(t/speed + (M_PI/2)) * sinMod;
  // input(0,2) = sin(t/speed + (M_PI)) * sinMod;     // zwei Inputs erscheinen besser
  // input(0,3) = sin(t/speed + (3*M_PI/2)) * sinMod;

  output = neural->forward(input);

  for (int i = 0; i < outputSize; ++i)
  {
    motors[i+2] = 2 * output(0,i) - 1;
  }

 // option to write all motor-values into file
 /*motorFile << t << "\t";
 for (int i = 0; i < outputSize; ++i)
 {
   motorFile << 2 * output(0,i) - 1 << "\t";
 }
 motorFile << endl;*/
};

/**
 * reset all variables for the next network
 */
void WalkController::startOfNewNet() {
  t = 0;
  penalty = 0;
  averageSpeed = 0;
  totalSpeed = 0;

  // move to next network
  curNetID++;
}

/**
 * change variables at the end of each time-step
 */
void WalkController::endOfStep () {
  t++;
  totalTime++;
}

/**
 * only here for compatibility reasons
 */
void WalkController::stepNoLearning(const sensor* sensors, int number_sensors,
                                    motor* motors, int number_motors) {

  // the old definition
  //double w = t/speed;
  // Horse Walk from wikipedia
  /* The walk is a four-beat gait that averages about 4 mph.
     When walking, a horse's legs follow this sequence:
     left hind leg, left front leg, right hind leg, right front leg,
     in a regular 1-2-3-4 beat. .... */
  /*
  double phases[4]= { w + 2*(M_PI/2),
                      w + 0*(M_PI/2),
                      w + 3*(M_PI/2),
                      w + 1*(M_PI/2) };

  motors[0] = sin(phases[0]+2)*0.1;
  motors[1] = 0;

  // hips
  for(int i=0; i<4; i++)
    motors[i+2]=sin(phases[i])*hipamplitude;

  // knees
  for(int i=0; i<2; i++){
    motors[i+6]=sin(phases[i]+1.05)*kneeamplitude;
  }
  for(int i=2; i<4; i++){
    motors[i+6]=sin(phases[i]+1.8)*kneeamplitude;
  }
  // ankles
  for(int i=0; i<2; i++){
    motors[i+10]=sin(phases[i]+1.05)*0.8;
  }

  //rest sine wave
  for(int i=12; i<number_motors; i++){
    motors[i]=sin(phases[i%4])*0.77;
  }*/
};
