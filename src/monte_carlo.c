#include "monte_carlo.h"
#include "linear_congruential_random_generator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef DRAW
#define DRAW 1
#endif
#ifndef LOSS
#define LOSS 0
#endif

/**
 * This method calculates the UCB value for a node.
 * @param node The node for which the UCB is calculated.
 * @return The UCB value of the node.
 */
mc_real calcUCB(Node* node) {
  if (node->nVisits == 0) {
    return UCB_MAX;
  }
  return node->score / node->nVisits +
         sqrt(2 * log10(node->parent->nVisits) / node->nVisits);
}

/**
 * This method finds the node with the maximum UCB value among all children.
 * @param children The array of children from which the max UCB value is
 * determined.
 * @param nChildren The number of children.
 * @return The node with the maximum UCB.
 */
Node* findMaxUCB(Node* children, unsigned nChildren) {
  Node* maxUCBNode = children;
  for (unsigned i = 0; i < nChildren; i++) {
    if (calcUCB(&children[i]) > calcUCB(maxUCBNode)) {
      maxUCBNode = &children[i];
    }
  }
  return maxUCBNode;
}

/**
 * This method selects the next node to expand from the current nodes' children.
 * @param node The current node from which the next node is selected.
 * @return The selected node.
 */
Node* selectChildren(Node* node, Board* board, Game* game) {
  Node* currNode = node;
  while (currNode->children) {
    currNode = findMaxUCB(currNode->children, currNode->nChildren);
    game->playAction(board, &currNode->action);
    if (calcUCB(currNode) == UCB_MAX) {
      return currNode;
    }
  }
  return currNode;
}

void createChild(Node* child, Node* node, Action* action) {
  child->nVisits = 0;
  child->score = 0;
  child->action.player = action->player;
  child->action.xPos = action->xPos;
  child->action.yPos = action->yPos;
  child->parent = node;
  child->children = NULL;
  child->nChildren = 0;
}

/**
 * This method expands the leaf of the current action tree
 * @param node The current leaf to expand.
 * @param player The current player.
 * @param game The definition of the game played, the environment in which the
 * the machine learns.
 */
void expandLeaf(Node* node, Game game, Board* board) {
  // Do not expand leaf if it is a terminal node
  if ((game.getScore(board, node->action.player) > DRAW) ||
      game.isDone(board)) { // TODO change this (the 1) so that is set by user
    return;
  }

  int nPossibleActions = game.getNumPossibleActions(*board);
  Action possibleActions[nPossibleActions];
  game.getPossibleActions(*board, possibleActions);

  int nValidActions = 0;
  for (unsigned i = 0; i < nPossibleActions; i++) {
    if (game.isValidAction(board, &possibleActions[i], -node->action.player)) {
      nValidActions++;
    }
  }

  // Deep copy of parent
  node->children =
      (struct Node*)malloc(nValidActions * sizeof(node->children[0]));

  nValidActions = 0;
  for (unsigned i = 0; i < nPossibleActions; ++i) {
    if (game.isValidAction(board, &possibleActions[i], -node->action.player)) {
      createChild(&node->children[nValidActions], node, &possibleActions[i]);
      nValidActions++;
      node->nChildren++;
    }
  }
}

/**
 * This method executes a complete random playout of actions.
 * @param node The root node from which the episode is played out.
 * @param player The current player.
 * @param game The definition of the game played, the environment in which the
 * the machine learns.
 * @return The result of the random playout, LOSS, WIN or DRAW.
 */
int mcEpisode(Node* node, int initialPlayer, Game* game, Board* board) {
  int score = game->getScore(board, node->action.player);
  if (score > 1) {
    if (node->action.player == initialPlayer) {
      return score;
    } else {
      return LOSS;
    }
  }

  int varPlayer = -node->action.player;

  // Deep copy of board
  Board simulationBoard;
  simulationBoard.values = malloc(game->getBoardSize() * sizeof(int8_t));
  for (int i = 0; i < game->getBoardSize(); ++i) {
    simulationBoard.values[i] = board->values[i];
  }
  simulationBoard.nPlayers = board->nPlayers;

  int nPossibleActions = game->getNumPossibleActions(simulationBoard);
  Action possibleActions[nPossibleActions];
  game->getPossibleActions(simulationBoard, possibleActions);

  // Random playout
  while (nPossibleActions > 0) {
    // Pick random action
    int randomActionIdx =
        linear_congruential_random_generator() * nPossibleActions;
    if (game->isValidAction(board, &possibleActions[randomActionIdx],
                            varPlayer)) {
      // Play action
      game->playAction(&simulationBoard, &possibleActions[randomActionIdx]);

      // Remove action from possible actions
      nPossibleActions -= simulationBoard.nPlayers;
      game->removeAction(randomActionIdx, possibleActions, nPossibleActions);
      int score = game->getScore(&simulationBoard, varPlayer);
      if (score > 1) {
        free(simulationBoard.values);
        if (varPlayer == initialPlayer) {
          return score;
        } else {
          return LOSS;
        }
      }
      varPlayer = -(varPlayer);
    }
  }
  free(simulationBoard.values);
  return DRAW;
}

/**
 * This method backpropagates the result of the playout to the root of the
 * action tree.
 * @param node The leaf node from which the backpropagation begins.
 * @param score The score to backpropagate.
 */
void backpropagate(Node* node, int score) {
  node->nVisits++;
  node->score += score;

  Node* currentChild = node;
  Node* currentNode = node->parent;
  while (currentNode) {
    currentNode->nVisits += 1;
    currentNode->score += score;
    currentChild = currentNode;
    currentNode = currentChild->parent;
  }
}

/**
 * This method frees the memory used during the Monte Carlo algorithm.
 * @param node The root node from which the function starts freeing the memory.
 */
void freeMCTree(Node* node) {
  for (unsigned i = 0; i < node->nChildren; ++i) {
    freeMCTree(&node->children[i]);
  }
  free(node->children);
  node->children = NULL;
}

/**
 * This method executes the Monte Carlo algorithm.
 * @param board The board used for the game.
 * @param player The current player.
 * @param game The definition of the game played, the environment in which the
 * the machine learns.
 * @param minSim Minimum number of simulations to run.
 * @param maxSim Maximum number of simulations to run.
 * @return The board with the best action to play after the execution of the
 * Monte Carlo algorithm.
 */
Action mcGame(Board board, int player, Game game, int minSim, int maxSim,
              mc_real goalValue) {
  Node node = {.nVisits = 0,
               .score = 0,
               .action.player = -player,
               .action.xPos = 0,
               .action.yPos = 0,
               .nChildren = 0,
               .children = NULL,
               .parent = NULL};
  set_linear_congruential_generator_seed(time(NULL));

  Board tempBoard;
  tempBoard.values = malloc(game.getBoardSize() * sizeof(int8_t));
  int iterations = 0;
  while ((node.nVisits < minSim ||
          calcUCB(findMaxUCB(node.children, node.nChildren)) < goalValue) &&
         node.nVisits <= maxSim) {
    for (int i = 0; i < game.getBoardSize(); ++i) {
      tempBoard.values[i] = board.values[i];
    }
    tempBoard.nPlayers = board.nPlayers;
    Node* selectedNode = selectChildren(&node, &tempBoard, &game);
    expandLeaf(selectedNode, game, &tempBoard);
    int score = mcEpisode(selectedNode, player, &game, &tempBoard);
    backpropagate(selectedNode, score);
    iterations++;
  }
  free(tempBoard.values);

  for (int i = 0; i < node.nChildren; ++i) {
    printf("Child %d, UCB: %f, nVisits: %d, action: [%d, %d]\n", i,
           calcUCB(&node.children[i]), node.children[i].nVisits,
           node.children[i].action.xPos, node.children[i].action.yPos);
  }
  Action action = findMaxUCB(node.children, node.nChildren)->action;
  freeMCTree(&node);
  return action;
}
