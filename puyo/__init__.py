from puyo.board import Board
from puyo.beanfinder import BeanFinder
from puyo.gccontrol import GamecubeControl
from puyo.vision import Vision
from puyo import ai

AI_REGISTRY = {
    'simple_combo': ai.SimpleComboAI,
    'random': ai.RandomAI,
    'simple_greedy': ai.SimpleGreedyAI,
}

DEFAULT_AI_NAME = "simple_combo"
DEFAULT_AI = AI_REGISTRY[DEFAULT_AI_NAME]
