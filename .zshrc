# Basic ZSH Configuration with Styling

# Enable colors
autoload -U colors && colors

# History configuration
HISTSIZE=10000
SAVEHIST=10000
HISTFILE=~/.zsh_history
setopt HIST_IGNORE_ALL_DUPS
setopt HIST_FIND_NO_DUPS
setopt SHARE_HISTORY

# Enable auto-completion
autoload -Uz compinit
compinit
zstyle ':completion:*' menu select
zstyle ':completion:*' matcher-list 'm:{a-zA-Z}={A-Za-z}'
zstyle ':completion:*' list-colors "${(s.:.)LS_COLORS}"

# Prompt styling with git support
autoload -Uz vcs_info
precmd() { vcs_info }
setopt prompt_subst

zstyle ':vcs_info:git:*' formats '%F{yellow}(%b)%f '
zstyle ':vcs_info:*' enable git

PROMPT='%F{cyan}%n@%m%f:%F{green}%~%f ${vcs_info_msg_0_}
%F{blue}❯%f '

# Enable ls colors
export CLICOLOR=1
export LS_COLORS='di=34:ln=35:so=32:pi=33:ex=31:bd=34;46:cd=34;43:su=30;41:sg=30;46:tw=30;42:ow=30;43'

# Useful aliases
alias ls='ls --color=auto'
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias grep='grep --color=auto'
alias ..='cd ..'
alias ...='cd ../..'
alias ....='cd ../../..'

# Git aliases
alias gs='git status'
alias ga='git add'
alias gc='git commit'
alias gp='git push'
alias gl='git log --oneline --graph --decorate'
alias gd='git diff'

# Directory navigation
setopt AUTO_CD
setopt AUTO_PUSHD
setopt PUSHD_IGNORE_DUPS

# Syntax highlighting and auto-suggestions (if installed)
if [ -f /usr/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]; then
    source /usr/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh
fi

if [ -f /usr/share/zsh-autosuggestions/zsh-autosuggestions.zsh ]; then
    source /usr/share/zsh-autosuggestions/zsh-autosuggestions.zsh
fi

# Command correction
setopt CORRECT
setopt CORRECT_ALL

# Better command line editing
bindkey -e  # Use emacs keybindings
bindkey '^[[A' history-search-backward
bindkey '^[[B' history-search-forward

# Pintos PATH setup
export PATH="$PATH:/home/workspace/group0/src/utils"

# Pintos development aliases
alias rebuild-index='cd /home/workspace/group0/src/threads && bear -- make && cp compile_commands.json /home/workspace/group0/ && cd - && echo "✓ Code navigation index rebuilt!"'


# bun completions
[ -s "/run/host_virtiofs/Users/hienn/hienn/cs162-workspace/.workspace/.bun/_bun" ] && source "/run/host_virtiofs/Users/hienn/hienn/cs162-workspace/.workspace/.bun/_bun"

# bun
export BUN_INSTALL="$HOME/.bun"
export PATH="$BUN_INSTALL/bin:$PATH"

# bun
export BUN_INSTALL="$HOME/.bun"
export PATH="$BUN_INSTALL/bin:$PATH"
