#include <iostream>
#include <fstream>

#include <boost/program_options.hpp>
#include <boost/functional/hash.hpp>

#include <yaml-cpp/yaml.h>

#include <libMultiRobotPlanning/cbs_ta.hpp>
#include <libMultiRobotPlanning/next_best_assignment.hpp>
#include "timer.hpp"

using namespace libMultiRobotPlanning;

struct State
{
  State(int time, int x, int y)
    : time(time)
    , x(x)
    , y(y)
  {
  }

  bool operator==(const State& s) const {
    return time == s.time && x == s.x && y == s.y;
  }

  bool equalExceptTime(const State& s) const {
    return x == s.x && y == s.y;
  }

  friend std::ostream& operator<< ( std::ostream& os, const State& s)
  {
    return os << s.time << ": (" << s.x << "," << s.y << ")";
    // return os << "(" << s.x << "," << s.y << ")";
  }

  int time;
  int x;
  int y;
};

namespace std
{
template<>
struct hash<State> {
    size_t operator()(const State& s) const {
      size_t seed = 0;
      boost::hash_combine(seed, s.time);
      boost::hash_combine(seed, s.x);
      boost::hash_combine(seed, s.y);
      return seed;
    }
};
}

///
enum class Action
{
  Up,
  Down,
  Left,
  Right,
  Wait,
};

std::ostream& operator<< ( std::ostream& os, const Action& a)
{
  switch(a)
  {
    case Action::Up:
      os << "Up";
      break;
    case Action::Down:
      os << "Down";
      break;
    case Action::Left:
      os << "Left";
      break;
    case Action::Right:
      os << "Right";
      break;
    case Action::Wait:
      os << "Wait";
      break;
  }
  return os;
}

///

struct Conflict
{
  enum Type
  {
    Vertex,
    Edge,
  };

  int time;
  size_t agent1;
  size_t agent2;
  Type type;

  int x1;
  int y1;
  int x2;
  int y2;

  friend std::ostream& operator<< ( std::ostream& os, const Conflict& c)
  {
    switch(c.type) {
    case Vertex:
      return os << c.time << ": Vertex(" << c.x1 << "," << c.y1 << ")";
    case Edge:
      return os << c.time << ": Edge(" << c.x1 << "," << c.y1 << "," << c.x2 << "," << c.y2 << ")";
    }
    return os;
  }
};

struct VertexConstraint
{
  VertexConstraint(int time, int x, int y) : time(time), x(x), y(y) {}
  int time;
  int x;
  int y;

  bool operator<(const VertexConstraint& other) const {
    return std::tie(time, x, y) < std::tie(other.time, other.x, other.y);
  }

  bool operator==(const VertexConstraint& other) const {
    return std::tie(time, x, y) == std::tie(other.time, other.x, other.y);
  }

  friend std::ostream& operator<< ( std::ostream& os, const VertexConstraint& c)
  {
    return os << "VC(" << c.time << "," << c.x << "," << c.y << ")";
  }
};

namespace std
{
template<>
struct hash<VertexConstraint> {
    size_t operator()(const VertexConstraint& s) const {
      size_t seed = 0;
      boost::hash_combine(seed, s.time);
      boost::hash_combine(seed, s.x);
      boost::hash_combine(seed, s.y);
      return seed;
    }
};
}

struct EdgeConstraint
{
  EdgeConstraint(int time, int x1, int y1, int x2, int y2) : time(time), x1(x1), y1(y1), x2(x2), y2(y2) {}
  int time;
  int x1;
  int y1;
  int x2;
  int y2;

  bool operator<(const EdgeConstraint& other) const {
    return std::tie(time, x1, y1, x2, y2) < std::tie(other.time, other.x1, other.y1, other.x2, other.y2);
  }

  bool operator==(const EdgeConstraint& other) const {
    return std::tie(time, x1, y1, x2, y2) == std::tie(other.time, other.x1, other.y1, other.x2, other.y2);
  }

  friend std::ostream& operator<< ( std::ostream& os, const EdgeConstraint& c)
  {
    return os << "EC(" << c.time << "," << c.x1 << "," << c.y1 << "," << c.x2 << "," << c.y2 << ")";
  }
};

namespace std
{
template<>
struct hash<EdgeConstraint> {
    size_t operator()(const EdgeConstraint& s) const {
      size_t seed = 0;
      boost::hash_combine(seed, s.time);
      boost::hash_combine(seed, s.x1);
      boost::hash_combine(seed, s.y1);
      boost::hash_combine(seed, s.x2);
      boost::hash_combine(seed, s.y2);
      return seed;
    }
};
}

struct Constraints
{
  std::unordered_set<VertexConstraint> vertexConstraints;
  std::unordered_set<EdgeConstraint> edgeConstraints;

  void add(const Constraints& other)
  {
    vertexConstraints.insert(other.vertexConstraints.begin(), other.vertexConstraints.end());
    edgeConstraints.insert(other.edgeConstraints.begin(), other.edgeConstraints.end());
  }

  bool overlap(const Constraints& other)
  {
    std::vector<VertexConstraint> vertexIntersection;
    std::vector<EdgeConstraint> edgeIntersection;
    std::set_intersection(vertexConstraints.begin(), vertexConstraints.end(),
                          other.vertexConstraints.begin(), other.vertexConstraints.end(),
                          std::back_inserter(vertexIntersection));
    std::set_intersection(edgeConstraints.begin(), edgeConstraints.end(),
                          other.edgeConstraints.begin(), other.edgeConstraints.end(),
                          std::back_inserter(edgeIntersection));
    return   vertexIntersection.size() > 0
          || edgeIntersection.size() > 0;
  }


  friend std::ostream& operator<< ( std::ostream& os, const Constraints& c)
  {
    for (const auto& vc : c.vertexConstraints) {
      os << vc << std::endl;
    }
    for (const auto& ec : c.edgeConstraints) {
      os << ec << std::endl;
    }
    return os;
  }
};

struct Location
{
  Location(int x, int y) : x(x), y(y) {}
  int x;
  int y;

  bool operator<(const Location& other) const {
    return std::tie(x, y) < std::tie(other.x, other.y);
  }

  bool operator==(const Location& other) const {
    return std::tie(x, y) == std::tie(other.x, other.y);
  }

  friend std::ostream& operator<< ( std::ostream& os, const Location& c)
  {
    return os << "(" << c.x << "," << c.y << ")";
  }
};

namespace std
{
template<>
struct hash<Location> {
    size_t operator()(const Location& s) const {
      size_t seed = 0;
      boost::hash_combine(seed, s.x);
      boost::hash_combine(seed, s.y);
      return seed;
    }
};
}

#include "shortest_path_heuristic.hpp"

///
class Environment
{
public:
  Environment(
    size_t dimx,
    size_t dimy,
    const std::unordered_set<Location>& obstacles,
    const std::vector<State>& startStates,
    const std::vector<Location>& goals,
    size_t maxTaskAssignments,
    size_t agentsPerGroup)
    : m_dimx(dimx)
    , m_dimy(dimy)
    , m_obstacles(obstacles)
    , m_goals(goals)
    , m_agentIdx(0)
    , m_constraints(nullptr)
    , m_lastGoalConstraint(-1)
    , m_maxTaskAssignments(maxTaskAssignments)
    , m_numTaskAssignments(0)
    , m_highLevelExpanded(0)
    , m_lowLevelExpanded(0)
    , m_heuristic(dimx, dimy, obstacles)
  {
    for (size_t i = 0; i < startStates.size(); ++i) {
      size_t groupStart = floor(i / (float)agentsPerGroup) * agentsPerGroup;
      size_t groupEnd = groupStart + agentsPerGroup;
      std::cout << "group range: " << groupStart << " - " << groupEnd << std::endl;
      for (size_t j = 0; j < goals.size(); ++j) {
        // m_assignment.setCost(i, j, std::abs(startStates[i].x - goals[j].x) + std::abs(startStates[i].y - goals[j].y));
        if (groupStart <= j && groupEnd > j) {
          m_assignment.setCost(i, j, m_heuristic.getValue(Location(startStates[i].x, startStates[i].y), goals[j]));
        }
      }
    }
    m_assignment.solve();
  }

  void setLowLevelContext(
    size_t agentIdx,
    const Constraints* constraints,
    size_t task)
  {
    assert(constraints);
    m_agentIdx = agentIdx;
    m_task = task;
    m_constraints = constraints;
    m_lastGoalConstraint = -1;
    for (const auto& vc : constraints->vertexConstraints) {
      if (vc.x == m_goals[m_task].x && vc.y == m_goals[m_task].y) {
        m_lastGoalConstraint = std::max(m_lastGoalConstraint, vc.time);
      }
    }
  }

  int admissibleHeuristic(
    const State& s)
  {
    return m_heuristic.getValue(Location(s.x, s.y), m_goals[m_task]);
    // return   std::abs(s.x - m_goals[m_task].x)
    //        + std::abs(s.y - m_goals[m_task].y);
  }

  bool isSolution(
    const State& s)
  {
    return    s.x == m_goals[m_task].x
           && s.y == m_goals[m_task].y
           && s.time > m_lastGoalConstraint;
  }

  void getNeighbors(
    const State& s,
    std::vector< Neighbor<State, Action, int> >& neighbors)
  {
    // std::cout << "#VC " << constraints.vertexConstraints.size() << std::endl;
    // for(const auto& vc : constraints.vertexConstraints) {
    //   std::cout << "  " << vc.time << "," << vc.x << "," << vc.y << std::endl;
    // }
    neighbors.clear();
    {
      State n(s.time+1, s.x, s.y);
      if (stateValid(n) && transitionValid(s, n)) {
        neighbors.emplace_back(Neighbor<State, Action, int>(n, Action::Wait, 1));
      }
    }
    {
      State n(s.time+1, s.x-1, s.y);
      if (stateValid(n) && transitionValid(s, n)) {
        neighbors.emplace_back(Neighbor<State, Action, int>(n, Action::Left, 1));
      }
    }
    {
      State n(s.time+1, s.x+1, s.y);
      if (stateValid(n) && transitionValid(s, n)) {
        neighbors.emplace_back(Neighbor<State, Action, int>(n, Action::Right, 1));
      }
    }
    {
      State n(s.time+1, s.x, s.y+1);
      if (stateValid(n) && transitionValid(s, n)) {
        neighbors.emplace_back(Neighbor<State, Action, int>(n, Action::Up, 1));
      }
    }
    {
      State n(s.time+1, s.x, s.y-1);
      if (stateValid(n) && transitionValid(s, n)) {
        neighbors.emplace_back(Neighbor<State, Action, int>(n, Action::Down, 1));
      }
    }
  }

  bool getFirstConflict(
    const std::vector<PlanResult<State, Action, int> >& solution,
    Conflict& result)
  {
    int max_t = 0;
    for (size_t i = 0; i < solution.size(); ++i) {
      max_t = std::max<int>(max_t, solution[i].states.size()-1);
    }

    for (int t = 0; t < max_t; ++t) {
      // check drive-drive vertex collisions
      for (size_t i = 0; i < solution.size(); ++i) {
        State state1 = getState(i, solution, t);
        for (size_t j = i+1; j < solution.size(); ++j) {
          State state2 = getState(j, solution, t);
          if (state1.equalExceptTime(state2)) {
            result.time = t;
            result.agent1 = i;
            result.agent2 = j;
            result.type = Conflict::Vertex;
            result.x1 = state1.x;
            result.y1 = state1.y;
            // std::cout << "VC " << t << "," << state1.x << "," << state1.y << std::endl;
            return true;
          }
        }
      }
      // drive-drive edge (swap)
      for (size_t i = 0; i < solution.size(); ++i) {
        State state1a = getState(i, solution, t);
        State state1b = getState(i, solution, t+1);
        for (size_t j = i+1; j < solution.size(); ++j) {
          State state2a = getState(j, solution, t);
          State state2b = getState(j, solution, t+1);
          if (state1a.equalExceptTime(state2b) && state1b.equalExceptTime(state2a)) {
            result.time = t;
            result.agent1 = i;
            result.agent2 = j;
            result.type = Conflict::Edge;
            result.x1 = state1a.x;
            result.y1 = state1a.y;
            result.x2 = state1b.x;
            result.y2 = state1b.y;
            return true;
          }
        }
      }
    }

    return false;
  }

  void createConstraintsFromConflict(
    const Conflict& conflict,
    std::map<size_t, Constraints>& constraints)
  {
    if (conflict.type == Conflict::Vertex) {
      Constraints c1;
      c1.vertexConstraints.emplace(VertexConstraint(conflict.time, conflict.x1, conflict.y1));
      constraints[conflict.agent1] = c1;
      constraints[conflict.agent2] = c1;
    } else if (conflict.type == Conflict::Edge) {
      Constraints c1;
      c1.edgeConstraints.emplace(EdgeConstraint(conflict.time, conflict.x1, conflict.y1, conflict.x2, conflict.y2));
      constraints[conflict.agent1] = c1;
      Constraints c2;
      c2.edgeConstraints.emplace(EdgeConstraint(conflict.time, conflict.x2, conflict.y2, conflict.x1, conflict.y1));
      constraints[conflict.agent2] = c2;
    }
  }

  void nextTaskAssignment(
    std::vector<size_t>& tasks)
  {
    if (m_numTaskAssignments > m_maxTaskAssignments) {
      return;
    }

    std::map<size_t, size_t> solution;
    long cost = m_assignment.nextSolution(solution);
    if (solution.size() > 0) {
      std::cout << "nextTaskAssignment: cost: " << cost << std::endl;
      tasks.resize(solution.size()); // TODO: handle cases where we have fewer tasks than robots...
      for (const auto& s : solution) {
        tasks[s.first] = s.second;
        // std::cout << s.first << "->" << s.second << std::endl;
      }

      ++m_numTaskAssignments;
    }

    // for (const auto& t : tasks) {
    //   std::cout << "TA: " << t << std::endl;
    // }
  }

  void onExpandHighLevelNode(int /*cost*/)
  {
    m_highLevelExpanded++;
  }

  void onExpandLowLevelNode(
    const State& /*s*/,
    int /*fScore*/,
    int /*gScore*/)
  {
    m_lowLevelExpanded++;
  }

  int highLevelExpanded()
  {
    return m_highLevelExpanded;
  }

  int lowLevelExpanded() const
  {
    return m_lowLevelExpanded;
  }

  size_t numTaskAssignments() const
  {
    return m_numTaskAssignments;
  }


private:
  State getState(
    size_t agentIdx,
    const std::vector<PlanResult<State, Action, int> >& solution,
    size_t t)
  {
    assert(agentIdx < solution.size());
    if (t < solution[agentIdx].states.size()) {
      return solution[agentIdx].states[t].first;
    }
    assert(solution[agentIdx].states.size() > 0);
    return solution[agentIdx].states.back().first;
  }

  bool stateValid(
    const State& s)
  {
    assert(m_constraints);
    const auto& con = m_constraints->vertexConstraints;
    return    s.x >= 0
           && s.x < m_dimx
           && s.y >= 0
           && s.y < m_dimy
           && m_obstacles.find(Location(s.x, s.y)) == m_obstacles.end()
           && con.find(VertexConstraint(s.time, s.x, s.y)) == con.end();
  }

  bool transitionValid(
    const State& s1,
    const State& s2)
  {
    assert(m_constraints);
    const auto& con = m_constraints->edgeConstraints;
    return con.find(EdgeConstraint(s1.time, s1.x, s1.y, s2.x, s2.y)) == con.end();
  }

private:
  int m_dimx;
  int m_dimy;
  std::unordered_set<Location> m_obstacles;
  std::vector<Location> m_goals;
  size_t m_agentIdx;
  size_t m_task;
  const Constraints* m_constraints;
  int m_lastGoalConstraint;
  NextBestAssignment<size_t, size_t> m_assignment;
  size_t m_maxTaskAssignments;
  size_t m_numTaskAssignments;
  int m_highLevelExpanded;
  int m_lowLevelExpanded;
  ShortestPathHeuristic m_heuristic;
};

int main(int argc, char* argv[])
{
  namespace po = boost::program_options;
  // Declare the supported options.
  po::options_description desc("Allowed options");
  std::string inputFile;
  std::string outputFile;
  size_t maxTaskAssignments;
  size_t groupSize;
  desc.add_options()
      ("help", "produce help message")
      ("input,i", po::value<std::string>(&inputFile)->required(), "input file (YAML)")
      ("output,o", po::value<std::string>(&outputFile)->required(), "output file (YAML)")
      ("maxTaskAssignments", po::value<size_t>(&maxTaskAssignments)->default_value(1e9), "maximum number of task assignments to try")
      ("groupSize", po::value<size_t>(&groupSize)->default_value(1e9), "number of agents per group")
  ;

  try
  {
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << "\n";
      return 0;
    }
  }
  catch(po::error& e)
  {
    std::cerr << e.what() << std::endl << std::endl;
    std::cerr << desc << std::endl;
    return 1;
  }

  YAML::Node config = YAML::LoadFile(inputFile);


  std::unordered_set<Location> obstacles;
  std::vector<Location> goals;
  std::vector<State> startStates;

  const auto& dim = config["map"]["dimensions"];
  int dimx = dim[0].as<int>();
  int dimy = dim[1].as<int>();

  for (const auto& node : config["map"]["obstacles"]) {
    obstacles.insert(Location(node[0].as<int>(), node[1].as<int>()));
  }

  for (const auto& node : config["agents"]) {
    const auto& start = node["start"];
    const auto& goal = node["goal"];
    startStates.emplace_back(State(0, start[0].as<int>(), start[1].as<int>()));
    // std::cout << "s: " << startStates.back() << std::endl;
    goals.emplace_back(Location(goal[0].as<int>(), goal[1].as<int>()));
  }

  Environment mapf(dimx, dimy, obstacles, startStates, goals, maxTaskAssignments, groupSize);
  CBSTA<State, Action, int, Conflict, Constraints, size_t, Environment> cbs(mapf);
  std::vector<PlanResult<State, Action, int> > solution;

  Timer timer;
  bool success = cbs.search(
    startStates,
    solution);
  timer.stop();


  if (success) {
    std::cout << "Planning successful! " << std::endl;
    long cost = 0;
    long makespan = 0;
    for (const auto& s : solution) {
      cost += s.cost;
      makespan = std::max<long>(makespan, s.cost);
    }

    std::ofstream out(outputFile);
    out << "statistics:" << std::endl;
    out << "  cost: " << cost << std::endl;
    out << "  makespan: " << makespan << std::endl;
    out << "  runtime: " << timer.elapsedSeconds() << std::endl;
    out << "  highLevelExpanded: " << mapf.highLevelExpanded() << std::endl;
    out << "  lowLevelExpanded: " << mapf.lowLevelExpanded() << std::endl;
    out << "  numTaskAssignments: " << mapf.numTaskAssignments() << std::endl;
    out << "schedule:" << std::endl;
    for (size_t a = 0; a < solution.size(); ++a) {
      // std::cout << "Solution for: " << a << std::endl;
      // for (size_t i = 0; i < solution[a].actions.size(); ++i) {
      //   std::cout << solution[a].states[i].second << ": " << solution[a].states[i].first << "->" << solution[a].actions[i].first << "(cost: " << solution[a].actions[i].second << ")" << std::endl;
      // }
      // std::cout << solution[a].states.back().second << ": " << solution[a].states.back().first << std::endl;

      out << "  agent" << a << ":" << std::endl;
      for (size_t i = 0; i < solution[a].states.size(); ++i) {
        out << "    - x: " << solution[a].states[i].first.x << std::endl
            << "      y: " << solution[a].states[i].first.y << std::endl
            << "      t: " << solution[a].states[i].second << std::endl;
      }
    }
  } else {
    std::cout << "Planning NOT successful!" << std::endl;
  }

  return 0;
}