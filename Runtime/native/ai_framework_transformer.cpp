// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>

namespace noqeri_ai::framework {
namespace {
constexpr std::size_t base_vocab=96;
void check(bool x,const char* reason) {
    if(!x)throw std::runtime_error(std::string("Noqeri framework: ")+reason);
}
std::size_t symbol(unsigned char c) {
    if(c=='\n')return 0;
    check(c>=32 && c<=126,"tokenizer accepts printable ASCII and LF only");
    return std::size_t(c-31);
}
char from_symbol(std::size_t id) {
    check(id<base_vocab,"invalid base token");
    return id==0?'\n':static_cast<char>(id+31);
}
std::vector<std::size_t> base_encode(const std::string& text) {
    check(text.size()<=65536,"text exceeds token budget");
    std::vector<std::size_t> ids;
    ids.reserve(text.size());
    for(unsigned char c:text)ids.push_back(symbol(c));
    return ids;
}
void validate_tokenizer(const SubwordTokenizer& t) {
    check(t.merges.size()<=32,"too many BPE merges");
    for(std::size_t i=0;i<t.merges.size();++i) {
        const auto [a,b]=t.merges[i];
        check(a<base_vocab+i && b<base_vocab+i,"invalid BPE dependency");
    }
}
std::vector<std::size_t> apply_merge(const std::vector<std::size_t>& ids,
                                     std::pair<std::size_t,std::size_t> pair,
                                     std::size_t next) {
    std::vector<std::size_t> out;out.reserve(ids.size());
    for(std::size_t i=0;i<ids.size();++i) {
        if(i+1<ids.size() && ids[i]==pair.first && ids[i+1]==pair.second) {
            out.push_back(next);++i;
        }else out.push_back(ids[i]);
    }
    return out;
}
void verify_model(const TransformerModel& m) {
    validate_tokenizer(m.tokenizer);
    check(m.context>=1 && m.context<=16 && m.hidden>=2 && m.hidden<=24,
          "invalid transformer dimensions");
    const auto v=m.tokenizer.vocab_size(),d=m.hidden;
    const auto shape=[](const Matrix& x,std::size_t r,std::size_t c){
        check(x.rows==r && x.cols==c && x.values.size()==r*c,
              "incorrect model parameter shape");
        for(double value:x.values)check(std::isfinite(value) && std::abs(value)<=1e8,
                                       "invalid model parameter");
    };
    shape(m.embedding,v,d);
    shape(m.wq,d,d);shape(m.wk,d,d);shape(m.wv,d,d);shape(m.wo,d,d);
    shape(m.w1,d,d);shape(m.w2,d,d);shape(m.output,d,v);
}
std::vector<std::size_t> preceding(const std::vector<std::size_t>& ids,
                                   std::size_t end,std::size_t context) {
    std::vector<std::size_t> sequence(context,0);
    for(std::size_t i=0;i<context;++i) {
        const std::size_t back=context-i;
        if(end>=back)sequence[i]=ids[end-back];
    }
    return sequence;
}
struct Graph {
    Tape tape;
    std::size_t embeds=0,wq=0,wk=0,wv=0,wo=0,w1=0,w2=0,out=0,logits=0;
};
Graph forward(const TransformerModel& m,const std::vector<std::size_t>& ids,
              Backend backend) {
    Graph g;
    g.tape.set_backend(backend);
    g.embeds=g.tape.leaf(m.embedding);
    g.wq=g.tape.leaf(m.wq);g.wk=g.tape.leaf(m.wk);
    g.wv=g.tape.leaf(m.wv);g.wo=g.tape.leaf(m.wo);
    g.w1=g.tape.leaf(m.w1);g.w2=g.tape.leaf(m.w2);
    g.out=g.tape.leaf(m.output);
    const auto input=g.tape.gather_rows(g.embeds,ids);
    const auto q=g.tape.matmul(input,g.wq);
    const auto k=g.tape.matmul(input,g.wk);
    const auto v=g.tape.matmul(input,g.wv);
    const auto score=g.tape.matmul(q,g.tape.transpose(k));
    const auto scaled=g.tape.scale(score,1.0/std::sqrt(double(m.hidden)));
    const auto attention=g.tape.softmax(scaled,true);
    const auto attended=g.tape.matmul(attention,v);
    const auto residual=g.tape.add(input,g.tape.matmul(attended,g.wo));
    const auto ff=g.tape.tanh(g.tape.matmul(residual,g.w1));
    const auto enriched=g.tape.add(residual,g.tape.matmul(ff,g.w2));
    g.logits=g.tape.matmul(g.tape.last_row(enriched),g.out);
    return g;
}
void write_matrix(std::ostream& out,const Matrix& m) {
    out<<m.rows<<" "<<m.cols<<"\n";
    for(double x:m.values)out<<x<<" ";
    out<<"\n";
}
Matrix read_matrix(std::istream& in,std::size_t rows,std::size_t cols) {
    std::size_t r=0,c=0;in>>r>>c;
    check(bool(in) && r==rows && c==cols && r<=1048576/c,"incorrect serialized tensor shape");
    Matrix m(r,c);
    for(double& x:m.values)in>>x;
    check(bool(in),"truncated transformer tensor");
    return m;
}
std::vector<Matrix*> parameters(TransformerModel& m) {
    return {&m.embedding,&m.wq,&m.wk,&m.wv,&m.wo,&m.w1,&m.w2,&m.output};
}
} // namespace

std::vector<std::size_t> SubwordTokenizer::encode(const std::string& text) const {
    validate_tokenizer(*this);
    auto ids=base_encode(text);
    for(std::size_t i=0;i<merges.size();++i)
        ids=apply_merge(ids,merges[i],base_vocab+i);
    return ids;
}
std::string SubwordTokenizer::decode(const std::vector<std::size_t>& tokens) const {
    validate_tokenizer(*this);
    check(tokens.size()<=4096,"too many token ids");
    std::vector<std::string> expansion(base_vocab+merges.size());
    for(std::size_t i=0;i<base_vocab;++i)expansion[i]=std::string(1,from_symbol(i));
    for(std::size_t i=0;i<merges.size();++i) {
        const auto [left,right]=merges[i];
        expansion[base_vocab+i]=expansion[left]+expansion[right];
        check(expansion[base_vocab+i].size()<=65536,"token expansion exceeds limit");
    }
    std::string out;
    for(auto id:tokens) {
        check(id<expansion.size(),"token id outside vocabulary");
        check(out.size()+expansion[id].size()<=65536,"decoded output exceeds limit");
        out+=expansion[id];
    }
    return out;
}
SubwordTokenizer SubwordTokenizer::train(const std::string& corpus,std::size_t max_merges) {
    check(max_merges<=32,"BPE merge limit is 32");
    auto ids=base_encode(corpus);
    check(ids.size()>=16,"BPE corpus must contain at least 16 bytes");
    SubwordTokenizer tok;
    for(std::size_t i=0;i<max_merges;++i) {
        std::map<std::pair<std::size_t,std::size_t>,std::size_t> counts;
        for(std::size_t j=1;j<ids.size();++j)++counts[{ids[j-1],ids[j]}];
        std::size_t best=1;
        std::pair<std::size_t,std::size_t> chosen{0,0};
        for(const auto& [pair,count]:counts) {
            if(count>best) {best=count;chosen=pair;}
        }
        if(best<2)break;
        ids=apply_merge(ids,chosen,base_vocab+tok.merges.size());
        tok.merges.push_back(chosen);
    }
    validate_tokenizer(tok);
    return tok;
}
TransformerEvaluation evaluate_transformer(const TransformerModel& m,
                                            const std::string& text,Backend backend) {
    verify_model(m);
    check(text.size()>=2 && text.size()<=65536,"evaluation text outside 2..65536 bytes");
    const auto ids=m.tokenizer.encode(text);
    check(ids.size()>=2,"evaluation requires at least two tokens");
    const std::size_t positions=std::min<std::size_t>(256,ids.size()-1);
    long double loss_total=0;
    // Evenly spaced teacher-forced positions, deterministic and bounded.
    for(std::size_t i=0;i<positions;++i) {
        const std::size_t position=1+((i*(ids.size()-1))/positions);
        auto graph=forward(m,preceding(ids,position,m.context),backend);
        const auto loss=graph.tape.cross_entropy(graph.logits,ids[position]);
        const double value=graph.tape.value(loss).at(0,0);
        check(std::isfinite(value) && value>=0,"nonfinite evaluation loss");
        loss_total+=value;
    }
    TransformerEvaluation result;
    result.positions=positions;
    result.loss=static_cast<double>(loss_total/positions);
    check(std::isfinite(result.loss) && result.loss<=700,
          "evaluation perplexity outside finite numeric range");
    result.perplexity=std::exp(result.loss);
    return result;
}

TransformerModel train_transformer(const std::string& corpus,
                                   const TransformerOptions& opt,double* train_loss,
                                   TransformerTrainingReport* report) {
    check(corpus.size()>=16 && corpus.size()<=65536,
          "transformer corpus must contain 16..65536 bytes");
    check(opt.context>=1 && opt.context<=16 && opt.hidden>=2 && opt.hidden<=24 &&
          opt.steps>=1 && opt.steps<=10000 && opt.merges<=32 &&
          opt.batch_size>=1 && opt.batch_size<=16 &&
          opt.steps<=160000/opt.batch_size,
          "transformer training budget exceeded");
    check(std::isfinite(opt.learning_rate) && opt.learning_rate>=0.000001 &&
          opt.learning_rate<=0.1,"invalid transformer learning rate");
    check(opt.optimizer==TrainingOptimizer::SGD ||
          opt.optimizer==TrainingOptimizer::AdamW,"invalid optimizer");
    check(std::isfinite(opt.gradient_clip_norm) && opt.gradient_clip_norm>0 &&
          opt.gradient_clip_norm<=100,"invalid gradient clipping norm");
    check(std::isfinite(opt.weight_decay) && opt.weight_decay>=0 &&
          opt.weight_decay<=1,"invalid weight decay");
    check(opt.warmup_steps<opt.steps && std::isfinite(opt.minimum_learning_rate) &&
          opt.minimum_learning_rate>=0.000001 &&
          opt.minimum_learning_rate<=opt.learning_rate,
          "invalid learning rate schedule");
    check(std::isfinite(opt.validation_fraction) && opt.validation_fraction>=0 &&
          opt.validation_fraction<=0.3,"invalid validation fraction");
    // Split before fitting BPE to prevent validation text from influencing
    // vocabulary construction. Validation is never sampled by optimization.
    std::size_t split=corpus.size();
    if(opt.validation_fraction>0) {
        const auto heldout=static_cast<std::size_t>(
            std::floor(double(corpus.size())*opt.validation_fraction));
        check(heldout>=16 && split-heldout>=16,
              "validation split must contain at least 16 bytes per partition");
        split-=heldout;
    }
    const std::string train_text=corpus.substr(0,split);
    TransformerModel m;
    m.tokenizer=SubwordTokenizer::train(train_text,opt.merges);
    const auto ids=m.tokenizer.encode(train_text);
    check(ids.size()>=8,"tokenized training corpus too small");
    m.context=opt.context;m.hidden=opt.hidden;
    const auto v=m.tokenizer.vocab_size(),d=m.hidden;
    m.embedding=Matrix(v,d);m.wq=Matrix(d,d);m.wk=Matrix(d,d);
    m.wv=Matrix(d,d);m.wo=Matrix(d,d);
    m.w1=Matrix(d,d);m.w2=Matrix(d,d);m.output=Matrix(d,v);
    std::mt19937 rng(opt.seed);
    std::uniform_real_distribution<double> uniform(-0.08,0.08);
    auto p=parameters(m);
    for(Matrix* param:p)for(double& w:param->values)w=uniform(rng);
    AdamWOptimizer adamw(p);
    double sum_loss=0.0;
    std::size_t logged=0;
    const bool old_sgd=(opt.optimizer==TrainingOptimizer::SGD &&
                        opt.batch_size==1 && !opt.cosine_decay &&
                        opt.warmup_steps==0 && opt.gradient_clip_norm==1.0);
    for(std::size_t step=0;step<opt.steps;++step) {
        std::vector<Matrix> accumulated;
        accumulated.reserve(p.size());
        for(const Matrix* param:p)
            accumulated.emplace_back(param->rows,param->cols);
        for(std::size_t sample=0;sample<opt.batch_size;++sample) {
            // Target is never part of the source context.
            const auto position=1+std::size_t(rng())%(ids.size()-1);
            auto graph=forward(m,preceding(ids,position,opt.context),opt.backend);
            const auto loss=graph.tape.cross_entropy(graph.logits,ids[position]);
            const double value=graph.tape.value(loss).at(0,0);
            check(std::isfinite(value) && value>=0,"nonfinite training loss");
            graph.tape.backward(loss);
            const std::size_t handles[]={graph.embeds,graph.wq,graph.wk,graph.wv,
                                         graph.wo,graph.w1,graph.w2,graph.out};
            for(std::size_t k=0;k<p.size();++k) {
                const auto& grad=graph.tape.grad(handles[k]).values;
                for(std::size_t j=0;j<grad.size();++j) {
                    accumulated[k].values[j]+=
                        grad[j]/static_cast<double>(opt.batch_size);
                    check(std::isfinite(accumulated[k].values[j]) &&
                          std::abs(accumulated[k].values[j])<=1e8,
                          "nonfinite accumulated gradient");
                }
            }
            if(step+1>opt.steps-std::min<std::size_t>(100,opt.steps)) {
                sum_loss+=value/static_cast<double>(opt.batch_size);
            }
        }
        if(step+1>opt.steps-std::min<std::size_t>(100,opt.steps))++logged;
        std::vector<Matrix*> mutable_grads;
        std::vector<const Matrix*> grads;
        for(Matrix& g:accumulated) {mutable_grads.push_back(&g);grads.push_back(&g);}
        if(!old_sgd)clip_grad_norm(mutable_grads,opt.gradient_clip_norm);
        double rate=opt.learning_rate;
        if(opt.cosine_decay || opt.warmup_steps>0)
            rate=cosine_learning_rate(step,opt.steps,opt.learning_rate,
                      opt.cosine_decay?opt.minimum_learning_rate:opt.learning_rate,
                      opt.warmup_steps);
        if(opt.optimizer==TrainingOptimizer::AdamW) {
            AdamWOptions options;
            options.learning_rate=rate;
            options.weight_decay=opt.weight_decay;
            adamw.step(p,grads,options);
        } else {
            for(std::size_t k=0;k<p.size();++k)
                sgd_update(*p[k],*grads[k],rate,old_sgd?1.0:100.0);
        }
    }
    verify_model(m);
    const double trailing=sum_loss/static_cast<double>(logged);
    check(std::isfinite(trailing),"invalid trailing loss");
    TransformerTrainingReport result;
    result.train_loss=trailing;
    result.optimizer_steps=opt.steps;
    if(split<corpus.size()) {
        const auto validation=evaluate_transformer(
            m,corpus.substr(split),opt.backend);
        result.validation_loss=validation.loss;
        result.validation_perplexity=validation.perplexity;
        result.validation_positions=validation.positions;
    }
    if(train_loss)*train_loss=trailing;
    if(report)*report=result;
    return m;
}

std::string generate_transformer(const TransformerModel& m,const std::string& prompt,
                                 std::size_t tokens,double temperature,std::uint32_t seed,Backend backend) {
    verify_model(m);
    check(prompt.size()<=1024 && tokens>=1 && tokens<=64,"prompt or token budget exceeded");
    check(std::isfinite(temperature) && temperature>=0 && temperature<=2,
          "invalid generation temperature");
    auto ids=m.tokenizer.encode(prompt);
    std::vector<std::size_t> generated;
    std::mt19937 rng(seed);
    for(std::size_t i=0;i<tokens;++i) {
        const auto ctx=preceding(ids,ids.size(),m.context);
        const auto g=forward(m,ctx,backend);
        const Matrix& logits=g.tape.value(g.logits);
        std::size_t selected=0;
        if(temperature==0) {
            selected=std::size_t(std::max_element(logits.values.begin(),
                                                 logits.values.end())-logits.values.begin());
        } else {
            const double top=*std::max_element(logits.values.begin(),logits.values.end());
            std::vector<double> probability(logits.cols);double total=0;
            for(std::size_t j=0;j<logits.cols;++j) {
                probability[j]=std::exp((logits.values[j]-top)/temperature);
                total+=probability[j];
            }
            const double threshold=(double(rng())+0.5)/4294967296.0;
            double cumulative=0;selected=probability.size()-1;
            for(std::size_t j=0;j<probability.size();++j) {
                cumulative+=probability[j]/total;
                if(threshold<cumulative){selected=j;break;}
            }
        }
        generated.push_back(selected);
        ids.push_back(selected);
    }
    return m.tokenizer.decode(generated);
}
void save_transformer(const TransformerModel& m,const std::string& filename) {
    verify_model(m);
    check(!filename.empty(),"empty output filename");
    const auto pending=filename+".pending";
    {
        std::ofstream out(pending,std::ios::binary|std::ios::trunc);
        check(bool(out),"cannot open transformer model for writing");
        out<<std::setprecision(17)<<"NQTF1 "<<m.context<<" "<<m.hidden
           <<" "<<m.tokenizer.merges.size()<<"\n";
        for(const auto& [a,b]:m.tokenizer.merges)out<<a<<" "<<b<<"\n";
        write_matrix(out,m.embedding);
        write_matrix(out,m.wq);write_matrix(out,m.wk);
        write_matrix(out,m.wv);write_matrix(out,m.wo);
        write_matrix(out,m.w1);write_matrix(out,m.w2);
        write_matrix(out,m.output);
        out.flush();check(bool(out),"failed to write transformer model");
    }
    std::error_code ec;
    std::filesystem::rename(pending,filename,ec);
    if(ec) {std::filesystem::remove(pending);throw std::runtime_error("Noqeri framework: model rename failed");}
}
TransformerModel load_transformer(const std::string& path) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(path,ec);
    check(!ec && size>0 && size<=2097152,"missing or oversized transformer model");
    std::ifstream in(path,std::ios::binary);
    check(bool(in),"unable to open transformer model");
    std::string magic;
    TransformerModel m;std::size_t merges=0;
    in>>magic>>m.context>>m.hidden>>merges;
    check(bool(in) && magic=="NQTF1" && m.context>=1 && m.context<=16 &&
          m.hidden>=2 && m.hidden<=24 && merges<=32,"invalid transformer header");
    for(std::size_t i=0;i<merges;++i) {
        std::size_t a=0,b=0;in>>a>>b;
        check(bool(in) && a<base_vocab+i && b<base_vocab+i,"invalid tokenizer table");
        m.tokenizer.merges.emplace_back(a,b);
    }
    const auto d=m.hidden,v=m.tokenizer.vocab_size();
    m.embedding=read_matrix(in,v,d);
    m.wq=read_matrix(in,d,d);m.wk=read_matrix(in,d,d);
    m.wv=read_matrix(in,d,d);m.wo=read_matrix(in,d,d);
    m.w1=read_matrix(in,d,d);m.w2=read_matrix(in,d,d);
    m.output=read_matrix(in,d,v);
    std::string extra;
    check(!(in>>extra),"trailing model payload");
    verify_model(m);
    return m;
}
} // namespace noqeri_ai::framework
