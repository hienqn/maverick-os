import React, { useState } from 'react';
import styles from './styles.module.css';

export interface QuizOption {
  text: string;
  isCorrect: boolean;
}

export interface QuizQuestion {
  question: string;
  options: QuizOption[];
  explanation: string;
  codeSnippet?: string;
}

interface QuizProps {
  questions: QuizQuestion[];
  title?: string;
  topic?: string;
}

export default function Quiz({
  questions,
  title,
  topic,
}: QuizProps): JSX.Element {
  const [currentQuestion, setCurrentQuestion] = useState(0);
  const [selectedAnswer, setSelectedAnswer] = useState<number | null>(null);
  const [showExplanation, setShowExplanation] = useState(false);
  const [score, setScore] = useState(0);
  const [completed, setCompleted] = useState(false);

  const question = questions[currentQuestion];

  const handleSelect = (index: number) => {
    if (selectedAnswer !== null) return; // Already answered

    setSelectedAnswer(index);
    setShowExplanation(true);

    if (question.options[index].isCorrect) {
      setScore((s) => s + 1);
    }
  };

  const handleNext = () => {
    if (currentQuestion < questions.length - 1) {
      setCurrentQuestion((c) => c + 1);
      setSelectedAnswer(null);
      setShowExplanation(false);
    } else {
      setCompleted(true);
    }
  };

  const handleRestart = () => {
    setCurrentQuestion(0);
    setSelectedAnswer(null);
    setShowExplanation(false);
    setScore(0);
    setCompleted(false);
  };

  if (completed) {
    const percentage = Math.round((score / questions.length) * 100);
    return (
      <div className={styles.container}>
        <div className={styles.completed}>
          <h3 className={styles.completedTitle}>Quiz Complete!</h3>
          <div className={styles.scoreCircle}>
            <span className={styles.scoreNumber}>{percentage}%</span>
            <span className={styles.scoreLabel}>
              {score}/{questions.length}
            </span>
          </div>
          <p className={styles.feedback}>
            {percentage >= 80
              ? 'Excellent! You have a strong understanding of this topic.'
              : percentage >= 60
              ? 'Good job! Review the concepts you missed.'
              : 'Keep learning! Review the material and try again.'}
          </p>
          <button onClick={handleRestart} className={styles.restartButton}>
            Try Again
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        {title && <h4 className={styles.title}>{title}</h4>}
        {topic && <span className={styles.topic}>{topic}</span>}
        <div className={styles.progress}>
          Question {currentQuestion + 1} of {questions.length}
        </div>
      </div>

      <div className={styles.progressBar}>
        <div
          className={styles.progressFill}
          style={{
            width: `${((currentQuestion + 1) / questions.length) * 100}%`,
          }}
        />
      </div>

      <div className={styles.questionContent}>
        <p className={styles.questionText}>{question.question}</p>

        {question.codeSnippet && (
          <pre className={styles.codeSnippet}>
            <code>{question.codeSnippet}</code>
          </pre>
        )}

        <div className={styles.options}>
          {question.options.map((option, idx) => {
            let optionClass = styles.option;
            if (selectedAnswer !== null) {
              if (option.isCorrect) {
                optionClass += ` ${styles.correct}`;
              } else if (idx === selectedAnswer) {
                optionClass += ` ${styles.incorrect}`;
              }
            }

            return (
              <button
                key={idx}
                className={optionClass}
                onClick={() => handleSelect(idx)}
                disabled={selectedAnswer !== null}
              >
                <span className={styles.optionLabel}>
                  {String.fromCharCode(65 + idx)}
                </span>
                <span className={styles.optionText}>{option.text}</span>
              </button>
            );
          })}
        </div>

        {showExplanation && (
          <div
            className={`${styles.explanation} ${
              question.options[selectedAnswer!].isCorrect
                ? styles.explanationCorrect
                : styles.explanationIncorrect
            }`}
          >
            <strong>
              {question.options[selectedAnswer!].isCorrect
                ? '✓ Correct!'
                : '✗ Incorrect'}
            </strong>
            <p>{question.explanation}</p>
          </div>
        )}
      </div>

      {showExplanation && (
        <div className={styles.footer}>
          <button onClick={handleNext} className={styles.nextButton}>
            {currentQuestion < questions.length - 1
              ? 'Next Question →'
              : 'See Results'}
          </button>
        </div>
      )}
    </div>
  );
}
